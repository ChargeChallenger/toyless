#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/cyw43_arch.h"
#include "core/icc.h"
#include "core/log.h"
#include "core/core1_main.h"

int main(void) {
    stdio_init_all();
    log_init();

    log_printf("Toyless\n");

    icc_init();

    log_printf("[Core 0] Launching Core 0");
    multicore_launch_core1(core1_entry);

    while (true) {
        portal_cmd_t cmd;
        if (icc_poll_cmd(&cmd)) {
            portal_cmd_t cmd;
            if (icc_poll_cmd(&cmd)) {
                log_printf("[Core 0] Принята команда: тип %d, слот %d\n", cmd.type, cmd.slot);
    
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
}