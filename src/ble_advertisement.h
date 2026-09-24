#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct {
    bool hid_service, keyboard_appearance, ewin_name;
    char name[32];
} ble_advertisement_t;
// Parse one advertising or scan-response payload. Reject truncated AD structures.
bool ble_advertisement_parse(const uint8_t *data, size_t length, ble_advertisement_t *out);
bool ble_advertisement_is_keyboard_candidate(const ble_advertisement_t *ad);
