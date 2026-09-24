#include "bridge_bluetooth.h"
#include "ble_advertisement.h"
#include "keyboard.h"
#include "btstack.h"
#include "btstack_tlv.h"
#include "ble/le_device_db.h"
#include "bridge.h"
#include "hardware/watchdog.h"
#include <stdio.h>
#include <string.h>

#define PEER_TAG 0x4d4b4c45u
static enum { OFF, SCANNING, CONNECTING, SECURING, DISCOVERING, READY,
              WAITING, CANCELLING, CLOSING } state;
static btstack_packet_callback_registration_t hci_registration, sm_registration;
static btstack_timer_source_t timer;
static const btstack_tlv_t *tlv;
static void *tlv_context;
// Stable identity address, not a transient resolvable private address.
static uint8_t peer[7]; // address type, six address bytes
static bool saved;
static hci_con_handle_t handle = HCI_CON_HANDLE_INVALID;
static uint16_t hids_cid;
static uint8_t descriptors[2048];
static unsigned scan_reports, scan_log_count;
static void start(void);
static void timeout(btstack_timer_source_t *ts);
static void gatt_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size);

bool bluetooth_ready(void) { return state == READY; }
static void arm(unsigned ms) {
    btstack_run_loop_remove_timer(&timer);
    timer.process = timeout;
    btstack_run_loop_set_timer(&timer, ms);
    btstack_run_loop_add_timer(&timer);
}
static void retry(void) {
    handle = HCI_CON_HANDLE_INVALID; hids_cid = 0;
    keyboard_release(); state = WAITING; arm(3000);
}
static void disconnect_peer(void) {
    keyboard_release(); state = CLOSING; arm(5000);
    if (handle != HCI_CON_HANDLE_INVALID) gap_disconnect(handle);
    else retry();
}
static void timeout(btstack_timer_source_t *ts) {
    (void)ts;
    switch (state) {
    case SCANNING:
        printf("BLE scan: %u reports; no candidate yet. Check pairing mode/Mac connection.\n", scan_reports);
        arm(15000); break;
    case WAITING: start(); break;
    case CONNECTING:
        printf("BLE connect timed out (%s)\n", saved ? "saved bond; GP15 resets it" : "new peer");
        state = CANCELLING; arm(5000); gap_connect_cancel(); break;
    case SECURING: case DISCOVERING:
        printf("BLE pairing/service timeout\n"); disconnect_peer(); break;
    case CANCELLING: case CLOSING:
        printf("BLE teardown stalled; rebooting\n"); watchdog_reboot(0, 0, 10); break;
    default: break;
    }
}
static void start(void) {
    if (saved) {
        printf("BLE reconnect saved identity %s type=%u\n", bd_addr_to_str(peer + 1), peer[0]);
        gap_whitelist_clear();
        uint8_t status = gap_whitelist_add((bd_addr_type_t)peer[0], peer + 1);
        state = CONNECTING; arm(15000);
        if (!status) status = gap_connect_with_whitelist();
        if (status) { printf("BLE reconnect error: %02x\n", status); retry(); }
        return;
    }
    printf("Scanning BLE: HID UUID, keyboard appearance, or EWIN BT5.1 Keyboard name\n");
    state = SCANNING;
    scan_reports = scan_log_count = 0;
    gap_set_scan_parameters(1, 0x60, 0x30); // Active: also examine scan responses.
    gap_start_scan();
    arm(15000);
}
static bool has_keyboard(uint8_t service) {
    const uint8_t *desc = hids_client_descriptor_storage_get_descriptor_data(hids_cid, service);
    uint16_t len = hids_client_descriptor_storage_get_descriptor_len(hids_cid, service);
    if (!desc || !len) return false;
    btstack_hid_usage_iterator_t it;
    btstack_hid_usage_iterator_init(&it, desc, len, HID_REPORT_TYPE_INPUT);
    while (btstack_hid_usage_iterator_has_more(&it)) {
        btstack_hid_usage_item_t item;
        btstack_hid_usage_iterator_get_item(&it, &item);
        if (item.usage_page == 7) return true;
    }
    return false;
}
static void save_identity(void) {
    int index = sm_le_device_index(handle);
    if (index < 0 || !gap_bonded(handle)) {
        printf("BLE peer is not bonded; pairing will be needed again\n"); return;
    }
    int addr_type; bd_addr_t address; sm_key_t irk;
    le_device_db_info(index, &addr_type, address, irk);
    if (addr_type != BD_ADDR_TYPE_LE_PUBLIC && addr_type != BD_ADDR_TYPE_LE_RANDOM) return;
    bool changed = !saved || peer[0] != addr_type || memcmp(peer + 1, address, 6);
    peer[0] = addr_type; memcpy(peer + 1, address, 6);
    saved = true;
    if (changed && tlv && tlv->store_tag(tlv_context, PEER_TAG, peer, sizeof(peer)))
        printf("BLE peer save failed\n");
    gap_load_resolving_list_from_le_device_db();
}
static void gatt_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size) {
    (void)channel; (void)size;
    if (type != HCI_EVENT_PACKET || hci_event_packet_get_type(packet) != HCI_EVENT_GATTSERVICE_META) return;
    switch (hci_event_gattservice_meta_get_subevent_code(packet)) {
    case GATTSERVICE_SUBEVENT_HID_SERVICE_CONNECTED: {
        if (state != DISCOVERING || gattservice_subevent_hid_service_connected_get_hids_cid(packet) != hids_cid) break;
        uint8_t status = gattservice_subevent_hid_service_connected_get_status(packet);
        if (status) { printf("BLE HID service error: %02x\n", status); disconnect_peer(); break; }
        bool keyboard = false;
        unsigned count = gattservice_subevent_hid_service_connected_get_num_instances(packet);
        for (unsigned i = 0; i < count; ++i) {
            bool kbd = has_keyboard(i);
            printf("BLE HID instance=%u descriptor=%u bytes keyboard=%u\n", i,
                hids_client_descriptor_storage_get_descriptor_len(hids_cid, i), kbd);
            keyboard |= kbd;
        }
        if (!keyboard) { printf("BLE HID has no keyboard input report\n"); disconnect_peer(); break; }
        save_identity(); state = READY;
        btstack_run_loop_remove_timer(&timer);
        printf("BLE keyboard ready\n"); break;
    }
    case GATTSERVICE_SUBEVENT_HID_REPORT: {
        if (state != READY || gattservice_subevent_hid_report_get_hids_cid(packet) != hids_cid) break;
        uint8_t service = gattservice_subevent_hid_report_get_service_index(packet);
        keyboard_ble_input(service,
            hids_client_descriptor_storage_get_descriptor_data(hids_cid, service),
            hids_client_descriptor_storage_get_descriptor_len(hids_cid, service),
            gattservice_subevent_hid_report_get_report(packet),
            gattservice_subevent_hid_report_get_report_len(packet));
        break;
    }
    // HCI disconnect is the single owner of reconnect scheduling.
    case GATTSERVICE_SUBEVENT_HID_SERVICE_DISCONNECTED:
        if (state == READY || state == DISCOVERING) disconnect_peer();
        break;
    default: break;
    }
}
static void secured(uint8_t status) {
    if (state != SECURING) return;
    if (status || !gap_encryption_key_size(handle)) {
        printf("BLE security failed: %02x; use GP15 reset if bond is stale\n", status);
        disconnect_peer(); return;
    }
    state = DISCOVERING; arm(30000);
    printf("BLE encrypted (key size=%u); discovering HID services\n", gap_encryption_key_size(handle));
    status = hids_client_connect(handle, gatt_handler, HID_PROTOCOL_MODE_REPORT, &hids_cid);
    if (status) { printf("BLE HIDS start failed: %02x\n", status); disconnect_peer(); }
}
static void sm_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size) {
    (void)channel; (void)size;
    if (type != HCI_EVENT_PACKET || state != SECURING) return;
    switch (hci_event_packet_get_type(packet)) {
    case SM_EVENT_JUST_WORKS_REQUEST:
        if (sm_event_just_works_request_get_handle(packet) == handle) {
            printf("BLE pairing: Just Works\n"); sm_just_works_confirm(handle);
        }
        break;
    case SM_EVENT_PASSKEY_DISPLAY_NUMBER:
        if (sm_event_passkey_display_number_get_handle(packet) != handle) break;
        printf("PAIR: type %06lu then Enter on the BLE keyboard\n",
               (unsigned long)sm_event_passkey_display_number_get_passkey(packet)); arm(120000); break;
    case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
        // Display-only has no confirmation input; do not claim a comparison was checked.
        if (sm_event_numeric_comparison_request_get_handle(packet) == handle) sm_bonding_decline(handle);
        break;
    case SM_EVENT_PASSKEY_INPUT_NUMBER:
        if (sm_event_passkey_input_number_get_handle(packet) != handle) break;
        printf("BLE peer requires local passkey entry; display-only bridge cannot accept it\n");
        sm_bonding_decline(handle); break;
    case SM_EVENT_PAIRING_COMPLETE:
        if (sm_event_pairing_complete_get_handle(packet) == handle) {
            printf("BLE pairing complete: status=%02x reason=%02x\n",
                sm_event_pairing_complete_get_status(packet), sm_event_pairing_complete_get_reason(packet));
            secured(sm_event_pairing_complete_get_status(packet));
        }
        break;
    case SM_EVENT_REENCRYPTION_COMPLETE:
        if (sm_event_reencryption_complete_get_handle(packet) == handle) secured(sm_event_reencryption_complete_get_status(packet));
        break;
    default: break;
    }
}
static void hci_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size) {
    (void)channel; (void)size;
    if (type != HCI_EVENT_PACKET) return;
    switch (hci_event_packet_get_type(packet)) {
    case HCI_EVENT_COMMAND_COMPLETE: {
        uint16_t opcode = hci_event_command_complete_get_command_opcode(packet);
        if (size < 6) break;
        // LE Set Scan Parameters / LE Set Scan Enable. Do not dump pairing keys.
        if (opcode == 0x200b || opcode == 0x200c)
            printf("BLE scan command %04x status=%02x\n", opcode, packet[5]);
        break;
    }
    case HCI_EVENT_COMMAND_STATUS:
        if (hci_event_command_status_get_status(packet))
            printf("HCI command %04x failed: %02x\n",
                hci_event_command_status_get_command_opcode(packet), hci_event_command_status_get_status(packet));
        break;
    case BTSTACK_EVENT_STATE:
        if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
            gap_load_resolving_list_from_le_device_db(); start();
        }
        break;
    case GAP_EVENT_ADVERTISING_REPORT: {
        if (state != SCANNING) break;
        const uint8_t *ad = gap_event_advertising_report_get_data(packet);
        uint8_t len = gap_event_advertising_report_get_data_length(packet);
        ++scan_reports;
        ble_advertisement_t info;
        if (!ble_advertisement_parse(ad, len, &info)) break;
        bd_addr_t addr; gap_event_advertising_report_get_address(packet, addr);
        bool candidate = ble_advertisement_is_keyboard_candidate(&info);
        uint8_t event_type = gap_event_advertising_report_get_advertising_event_type(packet);
        // Non-connectable and scannable-only advertisements cannot initiate a link.
        bool connectable = event_type == 0 || event_type == 1 || event_type == 4;
        if ((candidate || info.name[0]) && scan_log_count < 12) {
            ++scan_log_count;
            printf("BLE ADV %s type=%u event=%u name='%s' hid=%u keyboard=%u ewin=%u\n",
                bd_addr_to_str(addr), gap_event_advertising_report_get_address_type(packet),
                event_type, info.name, info.hid_service, info.keyboard_appearance, info.ewin_name);
        }
        if (!candidate || !connectable) break;
        gap_stop_scan(); state = CONNECTING; arm(15000);
        printf("Connecting BLE HID %s\n", bd_addr_to_str(addr));
        uint8_t status = gap_connect(addr, gap_event_advertising_report_get_address_type(packet));
        if (status) { printf("BLE gap_connect rejected: %02x\n", status); retry(); }
        break;
    }
    case HCI_EVENT_META_GAP:
        if (hci_event_gap_meta_get_subevent_code(packet) != GAP_SUBEVENT_LE_CONNECTION_COMPLETE) break;
        if (state != CONNECTING && state != CANCELLING) break;
        uint8_t status = gap_subevent_le_connection_complete_get_status(packet);
        printf("BLE connection complete: status=%02x\n", status);
        if (status) { retry(); break; }
        handle = gap_subevent_le_connection_complete_get_connection_handle(packet);
        if (state == CANCELLING) { disconnect_peer(); break; }
        state = SECURING; arm(120000);
        printf("BLE handle=%04x; starting security\n", handle); sm_request_pairing(handle);
        break;
    case HCI_EVENT_DISCONNECTION_COMPLETE:
        if (hci_event_disconnection_complete_get_connection_handle(packet) != handle) break;
        printf("BLE disconnected: reason=%02x state=%u\n",
            hci_event_disconnection_complete_get_reason(packet), state); retry(); break;
    default: break;
    }
}
void bluetooth_init(bool forget) {
    l2cap_init(); sm_init(); gatt_client_init();
    sm_set_io_capabilities(IO_CAPABILITY_DISPLAY_ONLY);
    sm_set_authentication_requirements(SM_AUTHREQ_BONDING | SM_AUTHREQ_SECURE_CONNECTION);
    att_server_init(profile_data, NULL, NULL);
    hids_client_init(descriptors, sizeof(descriptors));
    hci_registration.callback = hci_handler; hci_add_event_handler(&hci_registration);
    sm_registration.callback = sm_handler; sm_add_event_handler(&sm_registration);
    btstack_tlv_get_instance(&tlv, &tlv_context);
    if (forget) {
        if (tlv) tlv->delete_tag(tlv_context, PEER_TAG);
        for (int i = 0; i < le_device_db_max_count(); ++i) le_device_db_remove(i);
        printf("BLE pairings deleted\n");
    }
    saved = tlv && tlv->get_tag(tlv_context, PEER_TAG, peer, sizeof(peer)) == sizeof(peer)
        && (peer[0] == BD_ADDR_TYPE_LE_PUBLIC || peer[0] == BD_ADDR_TYPE_LE_RANDOM);
    printf("MachiKania BLE HID-over-GATT bridge / EWIN discovery v2 / %s %s\n", __DATE__, __TIME__);
}
