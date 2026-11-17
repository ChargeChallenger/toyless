#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/cyw43_arch.h"
#include "core/icc.h"
#include "core/log.h"
#include "core/core1_main.h"
#include "auth/xsm3_service.h"
#include "usb/usb_transport.h"
#include "protocol/infinity_engine.h"

int main(void) {
    stdio_init_all();
    log_init();

    log_printf("\n=========================================\n");
    log_printf("  Toyless Disney Infinity Base (Dual-Core)\n");
    log_printf("=========================================\n");

    // 1. Initialize Inter-Core Communication
    icc_init();

    // 2. Initialize DI Protocol Engine
    log_printf("[Core 0] Initalizing Infinity Protocol Engine...\n");
    infinity_engine_init();
    
    // 3. Initialize XSM3 authentication service
    log_printf("[Core 0] Initializing XSM3 service...\n");
    xsm3_service_init();

    // 4. Initialize USB transport
    log_printf("[Core 0] Initializing USB transport...\n");
    usb_transport_init();

    // 5. Launch Core 1 (Wi-Fi, storage, web)
    log_printf("[Core 0] Launching Core 1...\n");
    multicore_launch_core1(core1_entry);

    // 5. Core 0 real-time loop
    while (true) {
        usb_transport_task();

        portal_cmd_t cmd;
        if (icc_poll_cmd(&cmd)) {
            if (cmd.type == PORTAL_CMD_SET_FIGURE) {
                log_printf("[Core 0] Loading figure into slot %d\n", cmd.slot);
                infinity_load_figure(cmd.slot, cmd.figure_data);
            } else if (cmd.type == PORTAL_CMD_REMOVE_FIGURE) {
                log_printf("[Core 0] Removing figure from slot %d\n", cmd.slot);
                infinity_remove_figure(cmd.slot);
            }
        }

        if (infinity_figures_dirty()) {
            for (uint8_t i = 0; i < INFINITY_NUM_SLOTS; i++) {
                if (infinity_slot_dirty(i)) {
                    infinity_slot_clear_dirty(i);
                    portal_event_t evt = {
                        .type = PORTAL_EVT_FIGURE_DIRTY,
                        .slot = i,
                    };
                    icc_send_event(&evt);
                    log_printf("[Core 0] Figure in slot %d dirty, notifying Core 1\n", i);
                }
            }
            infinity_clear_dirty();
        }

        log_flush_uart();
        tight_loop_contents();
    }
}
