#include "core1_main.h"
#include "icc.h"
#include "log.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "storage/storage_manager.h"
#include "net/wifi_manager.h"

void core1_entry(void)
{
    log_printf("[Core 1] Core 1 thread started\n");

    // Initialize storage manager on Core 1
    storage_manager_init();

    // Initialize Wi-Fi manager
    wifi_manager_init();

    uint32_t last_heartbeat = 0;

    while (true)
    {
        // Periodic storage manager task (handles debounced saves)
        storage_manager_task();

        // Wi-Fi state machine task
        wifi_manager_task();

        cyw43_arch_poll();

        portal_event_t evt;
        if (icc_poll_event(&evt))
        {
            if (evt.type == PORTAL_EVT_FIGURE_DIRTY)
            {
                log_printf("[Core 1] Event: Figure in slot %d dirty (save required)\n", evt.slot);
                storage_manager_mark_dirty(evt.slot);
            }
            else if (evt.type == PORTAL_EVT_AUTH_SUCCESS)
            {
                log_printf("[Core 1] Event: Xbox 360 XSM3 authentication successful!\n");
                wifi_manager_trigger_auth_success();
            }
        }

        // Heartbeat LED blink every 1 second
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_heartbeat >= 1000)
        {
            last_heartbeat = now;
            if (wifi_manager_get_status().is_connected)
            {
                static bool led = false;
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, (led = !led));
            }
        }

        sleep_ms(10);
    }
}
