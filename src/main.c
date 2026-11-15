#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/cyw43_arch.h"
#include "core/icc.h"
#include "core/log.h"
#include "core/core1_main.h"
#include "auth/xsm3_service.h"
#include "usb/usb_transport.h"

int main(void) {
    stdio_init_all();
    log_init();

    log_printf("\n=========================================\n");
    log_printf("  Toyless Disney Infinity Base (Dual-Core)\n");
    log_printf("=========================================\n");

    // 1. Initialize Inter-Core Communication
    icc_init();

    // 2. Initialize XSM3 authentication service
    log_printf("[Core 0] Initializing XSM3 service...\n");
    xsm3_service_init();

    // 3. Initialize USB transport
    log_printf("[Core 0] Initializing USB transport...\n");
    usb_transport_init();

    // 4. Launch Core 1 (Wi-Fi, storage, web)
    log_printf("[Core 0] Launching Core 1...\n");
    multicore_launch_core1(core1_entry);

    // 5. Core 0 real-time loop
    while (true) {
        usb_transport_task();

        portal_cmd_t cmd;
        if (icc_poll_cmd(&cmd)) {
            log_printf("[Core 0] Command received: type %d, slot %d\n", cmd.type, cmd.slot);

            portal_event_t evt = {
                .type = PORTAL_EVT_FIGURE_DIRTY,
                .slot = cmd.slot,
                .block_index = 1,
            };
            icc_send_event(&evt);
        }

        log_flush_uart();
        tight_loop_contents();
    }
}
