#include "keyboard.h"
#include "btstack_hid_parser.h"
#include <string.h>

// Retain each report ID separately: media reports must not release held keys.
static struct { bool used, fn; uint8_t service; uint16_t id; uint8_t keys[32]; } slots[8];
#ifndef BRIDGE_APPLE_KEYBOARD
#define BRIDGE_APPLE_KEYBOARD 0
#endif
// Latch the translation on key-down, so releasing Fn first cannot change a held key.
static uint8_t translated[224];
static bool apple_fn_usage(uint16_t page, uint16_t usage) {
    return BRIDGE_APPLE_KEYBOARD && usage == 3 &&
        (page == 0x00ff || page == 0xff00 || page == 0xff01);
}
static uint8_t translate_key(uint8_t key, bool fn) {
    if (!BRIDGE_APPLE_KEYBOARD || !fn) return key;
    switch (key) {
    case 0x2a: return 0x4c; // Backspace -> Forward Delete
    case 0x50: return 0x4a; // Left -> Home
    case 0x4f: return 0x4d; // Right -> End
    case 0x52: return 0x4b; // Up -> Page Up
    case 0x51: return 0x4e; // Down -> Page Down
    default: return key;   // F1-F12 remain function keys.
    }
}
static keyboard_report_t current, queue[64];
static unsigned head, count;
// Bluetooth HID boot keyboard reports use report ID 1 followed by eight bytes.
static const uint8_t boot_descriptor[] = {
    0x05,0x01,0x09,0x06,0xa1,0x01,0x85,0x01,
    0x05,0x07,0x19,0xe0,0x29,0xe7,0x15,0,0x25,1,
    0x75,1,0x95,8,0x81,2,0x75,8,0x95,1,0x81,3,
    0x19,0,0x29,0xff,0x15,0,0x26,0xff,0,0x75,8,0x95,6,0x81,0,0xc0
};
void keyboard_boot_input(const uint8_t *report, uint16_t len) {
    if (len != 9 || report[0] != 1) return;
    keyboard_input(0, boot_descriptor, sizeof(boot_descriptor), report, len);
}
void keyboard_ble_input(uint8_t service, const uint8_t *descriptor, uint16_t descriptor_len,
                        const uint8_t *report, uint16_t report_len) {
    if (!descriptor || !descriptor_len || !report || report_len < 2) return;
    // SDK 2.2.0 HIDS client ALWAYS prepends Report Reference ID, including ID 0.
    if (!btstack_hid_report_id_declared(descriptor, descriptor_len)) {
        if (report[0] != 0) return;
        ++report; --report_len;
    }
    keyboard_input(service, descriptor, descriptor_len, report, report_len);
}

static void enqueue(keyboard_report_t report) {
    if (count == 64) {
        // Fail safe on overload: release before resynchronizing the latest state.
        head = count = 0;
        queue[count++] = (keyboard_report_t){0};
    }
    queue[(head + count++) % 64] = report;
}
void keyboard_snapshot(keyboard_report_t *report) { *report = current; }
bool keyboard_peek(keyboard_report_t *report) {
    if (!count) return false;
    *report = queue[head];
    return true;
}
void keyboard_pop(void) { if (count) { head = (head + 1) % 64; --count; } }
void keyboard_resync(void) { head = count = 0; enqueue((keyboard_report_t){0}); enqueue(current); }
void keyboard_release(void) {
    memset(slots, 0, sizeof(slots));
    memset(translated, 0, sizeof(translated));
    memset(&current, 0, sizeof(current));
    head = count = 0;
    enqueue(current);
}
void keyboard_input(uint8_t service, const uint8_t *descriptor, uint16_t descriptor_len,
                    const uint8_t *report, uint16_t report_len) {
    if (!descriptor || !descriptor_len || !report || !report_len) return;
    bool numbered = btstack_hid_report_id_declared(descriptor, descriptor_len);
    uint16_t id = numbered ? report[0] : HID_REPORT_ID_UNDEFINED;
    int expected = btstack_hid_get_report_size_for_id(id, HID_REPORT_TYPE_INPUT, descriptor, descriptor_len);
    if (expected <= 0 || report_len < expected + (numbered ? 1 : 0)) return;
    // Identify keyboard reports even if all key fields are zero (release).
    bool keyboard = false;
    btstack_hid_usage_iterator_t it;
    btstack_hid_usage_iterator_init(&it, descriptor, descriptor_len, HID_REPORT_TYPE_INPUT);
    while (btstack_hid_usage_iterator_has_more(&it)) {
        btstack_hid_usage_item_t item;
        btstack_hid_usage_iterator_get_item(&it, &item);
        if (item.report_id == id && (item.usage_page == 7 || apple_fn_usage(item.usage_page, item.usage))) keyboard = true;
    }
    if (!keyboard) return;
    int slot = -1, empty = -1;
    for (int i = 0; i < 8; ++i) {
        if (!slots[i].used) empty = i;
        else if (slots[i].service == service && slots[i].id == id) slot = i;
    }
    if (slot < 0) slot = empty;
    if (slot < 0) { keyboard_release(); return; }
    slots[slot].used = true; slots[slot].service = service; slots[slot].id = id;
    memset(slots[slot].keys, 0, 32);
    slots[slot].fn = false;
    btstack_hid_parser_t parser;
    btstack_hid_parser_init(&parser, descriptor, descriptor_len, HID_REPORT_TYPE_INPUT, report, report_len);
    while (btstack_hid_parser_has_more(&parser)) {
        uint16_t page, usage; int32_t value;
        btstack_hid_parser_get_field(&parser, &page, &usage, &value);
        if (page == 7 && value && usage && usage < 256)
            slots[slot].keys[usage / 8] |= 1u << (usage % 8);
        if (apple_fn_usage(page, usage) && value) slots[slot].fn = true;
    }
    uint8_t keys[32] = {0};
    bool fn = false;
    for (int i = 0; i < 8; ++i) {
        fn |= slots[i].fn;
        for (int j = 0; j < 32; ++j) keys[j] |= slots[i].keys[j];
    }
    uint8_t mapped[32] = {0};
    for (unsigned k = 4; k < 224; ++k) {
        if (!(keys[k / 8] & (1u << (k % 8)))) { translated[k] = 0; continue; }
        if (!translated[k]) translated[k] = translate_key(k, fn);
        unsigned target = translated[k];
        mapped[target / 8] |= 1u << (target % 8);
    }
    keyboard_report_t next = { .modifiers = keys[28] };
    unsigned n = 0;
    bool rollover = (keys[0] & 0x0e) != 0;
    for (unsigned k = 4; k < 224; ++k) {
        if (!(mapped[k / 8] & (1u << (k % 8)))) continue;
        if (n < 6) next.keys[n] = k;
        ++n;
    }
    if (n > 6 || rollover) memset(next.keys, 1, 6);
    if (memcmp(&current, &next, sizeof(next))) { current = next; enqueue(next); }
}
