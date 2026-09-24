#include "ble_advertisement.h"
#include <string.h>

static uint8_t lower(uint8_t c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
static bool ewin_name(const uint8_t *name, size_t n) {
    static const char expected[] = "ewin bt5.1 keyboard";
    if (n != sizeof(expected) - 1) return false;
    for (size_t i = 0; i < n; ++i) if (lower(name[i]) != expected[i]) return false;
    return true;
}
bool ble_advertisement_parse(const uint8_t *data, size_t length, ble_advertisement_t *out) {
    memset(out, 0, sizeof(*out));
    if (!data && length) return false;
    for (size_t pos = 0; pos < length;) {
        size_t size = data[pos++];
        if (!size) break;
        if (size > length - pos) { memset(out, 0, sizeof(*out)); return false; }
        uint8_t type = data[pos];
        const uint8_t *value = data + pos + 1;
        size_t n = size - 1;
        if (type == 0x02 || type == 0x03) { // 16-bit service UUID list
            if (n % 2) return false;
            for (size_t i = 0; i < n; i += 2)
                if (value[i] == 0x12 && value[i + 1] == 0x18) out->hid_service = true;
        } else if (type == 0x16 && n >= 2) { // Service Data - 16-bit UUID
            if (value[0] == 0x12 && value[1] == 0x18) out->hid_service = true;
        } else if (type == 0x19 && n == 2) { // GAP Appearance: HID Keyboard
            out->keyboard_appearance = value[0] == 0xc1 && value[1] == 0x03;
        } else if (type == 0x08 || type == 0x09) {
            // Only exact observed device name is a fallback, not arbitrary "keyboard" names.
            if (ewin_name(value, n)) out->ewin_name = true;
            if (type == 0x09 || !out->name[0]) {
                size_t copy = n < sizeof(out->name) - 1 ? n : sizeof(out->name) - 1;
                for (size_t i = 0; i < copy; ++i)
                    out->name[i] = value[i] >= 32 && value[i] <= 126 ? value[i] : '?';
                out->name[copy] = 0;
            }
        }
        pos += size;
    }
    return true;
}
bool ble_advertisement_is_keyboard_candidate(const ble_advertisement_t *ad) {
    return ad->hid_service || ad->keyboard_appearance || ad->ewin_name;
}
