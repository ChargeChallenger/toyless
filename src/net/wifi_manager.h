#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#define WIFI_AP_SSID_DEFAULT "toyless-portal"
#define WIFI_STARTUP_DELAY_MS 10000 

typedef enum
{
    WIFI_MODE_NONE = 0,
    WIFI_MODE_AP,
    WIFI_MODE_STA
} wifi_mode_t;

typedef struct
{
    wifi_mode_t mode;
    char ssid[33];
    char ip_str[16];
    bool is_connected;
} wifi_status_t;

void wifi_manager_init(void);
void wifi_manager_task(void);
void wifi_manager_trigger_auth_success(void);

wifi_status_t wifi_manager_get_status(void);
bool wifi_manager_set_sta_config(const char *ssid, const char *password);
bool wifi_manager_clear_sta_config(void);

#endif // WIFI_MANAGER_H
