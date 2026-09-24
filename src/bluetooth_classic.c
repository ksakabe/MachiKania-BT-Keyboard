#include "bridge_bluetooth.h"
#include "keyboard.h"
#include "btstack.h"
#include "btstack_tlv.h"
#include "hardware/watchdog.h"
#include <stdio.h>
#include <string.h>

#define PEER_TAG 0x4d4b4254u
#define PAIRING_TIMEOUT_MS 120000
static uint8_t descriptor_storage[2048];
static btstack_packet_callback_registration_t registration;
static btstack_timer_source_t retry_timer;
static const btstack_tlv_t *tlv;
static void *tlv_context;
static bd_addr_t peer;
static bool saved, descriptor_ready, boot_mode;
static uint16_t cid;
static unsigned inquiry_reports;
static enum { OFF, SCANNING, CONNECTING, CONNECTED, WAITING, CLOSING } state;
static const hid_protocol_mode_t mode = HID_PROTOCOL_MODE_REPORT_WITH_FALLBACK_TO_BOOT;

bool bluetooth_ready(void) { return state == CONNECTED && descriptor_ready; }
static void start(void);
static void retry(btstack_timer_source_t *timer) {
    (void)timer;
    if (state == CLOSING) {
        // The stack cannot cancel every intermediate SDP/L2CAP state.
        // A bounded recovery also resets USB and releases keys at the host.
        printf("Bluetooth teardown stalled; rebooting\n");
        watchdog_reboot(0, 0, 10);
        return;
    }
    if (state == CONNECTING) {
        printf("Connection/pairing timeout; retrying\n");
        // Let the HID close event serialize teardown before starting again.
        if (cid) {
            state = CLOSING;
            btstack_run_loop_set_timer(timer, 5000);
            btstack_run_loop_add_timer(timer);
            hid_host_disconnect(cid);
            return;
        }
    }
    start();
}
static void schedule(unsigned ms) {
    btstack_run_loop_remove_timer(&retry_timer);
    retry_timer.process = retry;
    btstack_run_loop_set_timer(&retry_timer, ms);
    btstack_run_loop_add_timer(&retry_timer);
}
static void connect_peer(void) {
    state = CONNECTING;
    descriptor_ready = boot_mode = false;
    printf("Connecting to %s\n", bd_addr_to_str(peer));
    uint8_t status = hid_host_connect(peer, mode, &cid);
    if (status) { printf("Connect error: 0x%02x\n", status); cid = 0; state = WAITING; schedule(3000); }
    else schedule(PAIRING_TIMEOUT_MS);
}
static void start(void) {
    if (saved) { printf("Classic saved peer: use GP15 at startup to select a different keyboard\n"); connect_peer(); return; }
    state = SCANNING;
    inquiry_reports = 0;
    printf("Searching for a Bluetooth Classic keyboard...\n");
    uint8_t status = gap_inquiry_start(5);
    if (status) { printf("Classic inquiry start failed: %02x\n", status); state = WAITING; schedule(3000); }
}
static bool peer_matches(bd_addr_t addr) { return memcmp(peer, addr, sizeof(bd_addr_t)) == 0; }
static void ready(void) {
    descriptor_ready = true; state = CONNECTED;
    btstack_run_loop_remove_timer(&retry_timer);
    if (!saved && tlv) {
        int result = tlv->store_tag(tlv_context, PEER_TAG, peer, sizeof(peer));
        if (result) printf("Could not save peer: %d\n", result);
    }
    saved = true;
    printf("Keyboard ready: %s\n", bd_addr_to_str(peer));
}
static void handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size) {
    (void)channel; (void)size;
    if (type != HCI_EVENT_PACKET) return;
    bd_addr_t addr;
    switch (hci_event_packet_get_type(packet)) {
    case BTSTACK_EVENT_STATE:
        if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) start();
        break;
    case GAP_EVENT_INQUIRY_RESULT: {
        if (state != SCANNING) break;
        ++inquiry_reports;
        uint32_t cod = gap_event_inquiry_result_get_class_of_device(packet);
        gap_event_inquiry_result_get_bd_addr(packet, addr);
        char name[80] = "(name not in inquiry response)";
        if (gap_event_inquiry_result_get_name_available(packet)) {
            unsigned n = gap_event_inquiry_result_get_name_len(packet);
            if (n >= sizeof(name)) n = sizeof(name) - 1;
            const uint8_t *input = gap_event_inquiry_result_get_name(packet);
            for (unsigned i = 0; i < n; ++i) name[i] = input[i] >= 32 && input[i] <= 126 ? input[i] : '?';
            name[n] = 0;
        }
        if (inquiry_reports <= 20)
            printf("Classic found %s class=%06lx name='%s'\n", bd_addr_to_str(addr), (unsigned long)cod, name);
        // Major class Peripheral, minor class Keyboard (also keyboard+pointing).
        if ((cod & 0x1f00) != 0x0500 || !(cod & 0x0040)) break;
        gap_event_inquiry_result_get_bd_addr(packet, peer);
        gap_inquiry_stop();
        connect_peer();
        break;
    }
    case GAP_EVENT_INQUIRY_COMPLETE:
        if (state == SCANNING) {
            printf("Classic inquiry complete: %u reports, no keyboard candidate\n", inquiry_reports);
            state = WAITING; schedule(1000);
        }
        break;
    case HCI_EVENT_PIN_CODE_REQUEST:
        hci_event_pin_code_request_get_bd_addr(packet, addr);
        if (peer_matches(addr)) {
            schedule(PAIRING_TIMEOUT_MS);
            printf("PAIR: type 0000 then Enter on the Bluetooth keyboard\n");
            gap_pin_code_response(addr, "0000");
        } else gap_pin_code_negative(addr);
        break;
    case HCI_EVENT_USER_PASSKEY_NOTIFICATION:
        hci_event_user_passkey_notification_get_bd_addr(packet, addr);
        if (peer_matches(addr)) {
            schedule(PAIRING_TIMEOUT_MS);
            printf("PAIR: type %06lu then Return/Enter on the Bluetooth keyboard\n",
                   (unsigned long)hci_event_user_passkey_notification_get_numeric_value(packet));
        }
        break;
    case HCI_EVENT_USER_CONFIRMATION_REQUEST:
        hci_event_user_confirmation_request_get_bd_addr(packet, addr);
        if (peer_matches(addr)) gap_ssp_confirmation_response(addr);
        else gap_ssp_confirmation_negative(addr);
        break;
    case HCI_EVENT_HID_META:
        switch (hci_event_hid_meta_get_subevent_code(packet)) {
        case HID_SUBEVENT_INCOMING_CONNECTION: {
            hid_subevent_incoming_connection_get_address(packet, addr);
            uint16_t incoming = hid_subevent_incoming_connection_get_hid_cid(packet);
            if ((saved && !peer_matches(addr)) || state == CONNECTED || (cid && cid != incoming)) {
                hid_host_decline_connection(incoming); break;
            }
            memcpy(peer, addr, sizeof(peer));
            if (state == SCANNING) gap_inquiry_stop();
            cid = incoming; state = CONNECTING; descriptor_ready = boot_mode = false;
            hid_host_accept_connection(cid, mode); schedule(PAIRING_TIMEOUT_MS);
            break;
        }
        case HID_SUBEVENT_CONNECTION_OPENED:
            if (hid_subevent_connection_opened_get_status(packet)) {
                printf("HID open failed: 0x%02x\n", hid_subevent_connection_opened_get_status(packet));
                cid = 0; descriptor_ready = false; state = WAITING;
                keyboard_release(); schedule(3000); break;
            }
            cid = hid_subevent_connection_opened_get_hid_cid(packet);
            // Keep timeout active until descriptor is available.
            printf("HID channels open\n");
            break;
        case HID_SUBEVENT_SET_PROTOCOL_RESPONSE:
            if (hid_subevent_set_protocol_response_get_handshake_status(packet) == HID_HANDSHAKE_PARAM_TYPE_SUCCESSFUL) {
                boot_mode = hid_subevent_set_protocol_response_get_protocol_mode(packet) == HID_PROTOCOL_MODE_BOOT;
                if (boot_mode) ready();
            } else {
                descriptor_ready = false; state = CLOSING; keyboard_release();
                schedule(5000); hid_host_disconnect(cid);
            }
            break;
        case HID_SUBEVENT_DESCRIPTOR_AVAILABLE:
            if (hid_subevent_descriptor_available_get_status(packet)) {
                // The stack may next negotiate boot mode. Retain connection timeout.
                printf("HID descriptor unavailable; waiting for boot fallback\n"); break;
            }
            if (hid_descriptor_storage_get_descriptor_len(cid)) ready();
            break;
        case HID_SUBEVENT_REPORT: {
            if (!descriptor_ready) break;
            const uint8_t *report = hid_subevent_report_get_report(packet);
            uint16_t len = hid_subevent_report_get_report_len(packet);
            if (!len || report[0] != 0xa1) break;
            if (boot_mode) { keyboard_boot_input(report + 1, len - 1); break; }
            const uint8_t *descriptor = hid_descriptor_storage_get_descriptor_data(cid);
            uint16_t dlen = hid_descriptor_storage_get_descriptor_len(cid);
            keyboard_input(0, descriptor, dlen, report + 1, len - 1);
            break;
        }
        case HID_SUBEVENT_CONNECTION_CLOSED:
            cid = 0; descriptor_ready = false; state = WAITING;
            keyboard_release(); printf("Disconnected; reconnecting\n"); schedule(3000);
            break;
        default: break;
        }
        break;
    default: break;
    }
}
void bluetooth_init(bool forget) {
    l2cap_init();
    hid_host_init(descriptor_storage, sizeof(descriptor_storage));
    hid_host_register_packet_handler(handler);
    registration.callback = handler;
    hci_add_event_handler(&registration);
    gap_set_local_name("MachiKania Keyboard Bridge");
    hci_set_inquiry_mode(INQUIRY_MODE_RSSI_AND_EIR);
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_SNIFF_MODE | LM_LINK_POLICY_ENABLE_ROLE_SWITCH);
    hci_set_master_slave_policy(HCI_ROLE_MASTER);
    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_DISPLAY_ONLY);
    gap_ssp_set_authentication_requirement(SSP_IO_AUTHREQ_MITM_PROTECTION_REQUIRED_DEDICATED_BONDING);
    gap_ssp_set_auto_accept(0);
    gap_discoverable_control(0);
    gap_connectable_control(1);
    btstack_tlv_get_instance(&tlv, &tlv_context);
    if (forget) {
        if (tlv) tlv->delete_tag(tlv_context, PEER_TAG);
        gap_delete_all_link_keys();
        printf("Saved pairing deleted\n");
    }
    saved = tlv && tlv->get_tag(tlv_context, PEER_TAG, peer, sizeof(peer)) == sizeof(peer);
#ifdef BRIDGE_APPLE_KEYBOARD
    printf("MachiKania type P / Apple Wireless Keyboard bridge (Classic HID)\n");
#else
    printf("MachiKania type P / Classic HID bridge / inquiry diagnostic v3 / %s %s\n", __DATE__, __TIME__);
#endif
}
