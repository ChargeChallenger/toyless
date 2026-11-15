#include "core1_main.h"
#include "icc.h"
#include "log.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

void core1_entry(void) {
    log_printf("[Core 1] Core 1 thread started\n");

    bool cyw43_ready = false;
    if (cyw43_arch_init() == 0) {
        cyw43_ready = true;
    } else {
        log_printf("[Core 1] Failed to initialize CYW43\n");
    }

    uint32_t last_cmd_time = 0;
    uint8_t demo_slot = 0;

    while (true) {
        portal_event_t evt;
        if (icc_poll_event(&evt)) {
            if (evt.type == PORTAL_EVT_FIGURE_DIRTY) {
                log_printf("[Core 1] Event: slot %d, modified block %d\n", evt.slot, evt.block_index);
            } else if (evt.type == PORTAL_EVT_AUTH_SUCCESS) {
                log_printf("[Core 1] Event: PORTAL_EVT_AUTH_SUCCESS received\n");
            }
        }

        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_cmd_time >= 2000) {
            last_cmd_time = now;

            portal_cmd_t cmd = {
                .type = PORTAL_CMD_SET_FIGURE,
                .slot = demo_slot,
            };
            icc_send_cmd(&cmd);
            log_printf("[Core 1] Sent command SET_FIGURE for slot %d\n", demo_slot);

            demo_slot = (demo_slot + 1) % 9;

            if (cyw43_ready) {
                static bool led = false;
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, (led = !led));
            }
        }

        sleep_ms(10);
    }
}
