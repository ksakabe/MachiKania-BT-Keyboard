#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { uint8_t modifiers, reserved, keys[6]; } keyboard_report_t;
void keyboard_input(uint8_t service, const uint8_t *descriptor, uint16_t descriptor_len,
                    const uint8_t *report, uint16_t report_len);
void keyboard_release(void);
void keyboard_boot_input(const uint8_t *report, uint16_t len);
void keyboard_ble_input(uint8_t service, const uint8_t *descriptor, uint16_t descriptor_len,
                        const uint8_t *report, uint16_t report_len);
bool keyboard_peek(keyboard_report_t *report);
void keyboard_pop(void);
void keyboard_snapshot(keyboard_report_t *report);
void keyboard_resync(void);
