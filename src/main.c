#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "btstack.h"
#include "usb.h"
#include "bridge_bluetooth.h"
#include "keyboard.h"
#include <stdio.h>

static btstack_timer_source_t tick;
static void service(btstack_timer_source_t *timer) {
    usb_task();
    static uint32_t last_led_update;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last_led_update >= 100) {
        last_led_update = now;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, bluetooth_ready() || ((now / 500) & 1));
    }
    btstack_run_loop_set_timer(timer, 1);
    btstack_run_loop_add_timer(timer);
}
int main(void) {
    stdio_init_all();
    // Hold GP15 to GND at power-on to forget the saved keyboard.
    gpio_init(15); gpio_set_dir(15, GPIO_IN); gpio_pull_up(15);
    sleep_ms(10);
    bool forget = !gpio_get(15);
    if (cyw43_arch_init()) { printf("CYW43 init failed\n"); return 1; }
    usb_init();
    bluetooth_init(forget);
    tick.process = service;
    btstack_run_loop_set_timer(&tick, 1);
    btstack_run_loop_add_timer(&tick);
    hci_power_control(HCI_POWER_ON);
    btstack_run_loop_execute();
}
