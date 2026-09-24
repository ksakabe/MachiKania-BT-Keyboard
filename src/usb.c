#include "tusb.h"
#include "pico/unique_id.h"
#include "pico/time.h"
#include "keyboard.h"
#include "usb.h"
#include <string.h>

// Development VID/PID; obtain your own VID/PID before distributing a product.
static const tusb_desc_device_t device = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200, .bMaxPacketSize0 = 64,
    .idVendor = 0xcafe, .idProduct = 0x4011, .bcdDevice = 0x0100,
    .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3, .bNumConfigurations = 1,
};
static const uint8_t report_descriptor[] = { TUD_HID_REPORT_DESC_KEYBOARD() };
static const uint8_t configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN, 0, 250),
    TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_KEYBOARD, sizeof(report_descriptor), 0x81, 8, 1),
};
const uint8_t *tud_descriptor_device_cb(void) { return (const uint8_t *)&device; }
const uint8_t *tud_descriptor_configuration_cb(uint8_t index) { (void)index; return configuration; }
const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance) { (void)instance; return report_descriptor; }
const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t out[40];
    static char serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
    const char *s;
    if (index == 0) { out[0] = 0x0304; out[1] = 0x0409; return out; }
    if (index == 1) s = "MachiKania";
    else if (index == 2) s = "Bluetooth Keyboard Bridge";
    else if (index == 3) { pico_get_unique_board_id_string(serial, sizeof(serial)); s = serial; }
    else return NULL;
    size_t n = strlen(s); if (n > 39) n = 39;
    out[0] = (TUSB_DESC_STRING << 8) | (2 * n + 2);
    for (size_t i = 0; i < n; ++i) out[i + 1] = (uint8_t)s[i];
    return out;
}
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t id, hid_report_type_t type, uint8_t *buffer, uint16_t len) {
    (void)instance;
    if (id || type != HID_REPORT_TYPE_INPUT) return 0;
    keyboard_report_t report; keyboard_snapshot(&report);
    if (len > sizeof(report)) len = sizeof(report);
    memcpy(buffer, &report, len); return len;
}
void tud_hid_set_report_cb(uint8_t instance, uint8_t id, hid_report_type_t type, const uint8_t *buffer, uint16_t len) {
    // Host lock state still works; forwarding LEDs to Bluetooth is not implemented.
    (void)instance; (void)id; (void)type; (void)buffer; (void)len;
}
void tud_mount_cb(void) { keyboard_resync(); }
void tud_umount_cb(void) { keyboard_resync(); }
void tud_resume_cb(void) { keyboard_resync(); }
static uint32_t last_sent;
static uint8_t idle_rate;
bool tud_hid_set_idle_cb(uint8_t instance, uint8_t rate) {
    (void)instance; idle_rate = rate; return true;
}
void usb_init(void) {
    const tusb_rhport_init_t init = { .role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL };
    tusb_init(0, &init);
}
void usb_task(void) {
    // Do not block the Bluetooth run loop while waiting for USB traffic.
    tud_task_ext(0, false);
    keyboard_report_t report;
    if (!tud_hid_ready()) return;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (keyboard_peek(&report)) {
        if (tud_hid_report(0, &report, sizeof(report))) { keyboard_pop(); last_sent = now; }
    } else if (idle_rate && now - last_sent >= (uint32_t)idle_rate * 4) {
        keyboard_snapshot(&report);
        if (tud_hid_report(0, &report, sizeof(report))) last_sent = now;
    }
}
