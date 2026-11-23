#include "wifi_manager.h"
#include "dhcpserver.h"
#include "dnsserver.h"
#include "core/log.h"
#include "web/web_server.h"
#include "storage/lfs_port.h"
#include "pico/cyw43_arch.h"
#include "pico/time.h"
#include <string.h>
#include <stdio.h>

#define WIFI_CFG_MAGIC 0x57494649 // "WIFI"
#define WIFI_CFG_PATH "/saves/wifi.cfg"

typedef struct
{
    uint32_t magic;
    char ssid[33];
    char password[65];
} wifi_cfg_file_t;

typedef enum
{
    WIFI_STATE_WAIT_TRIGGER = 0,
    WIFI_STATE_INIT,
    WIFI_STATE_CONNECTING_STA,
    WIFI_STATE_STA_CONNECTED,
    WIFI_STATE_AP_ACTIVE,
    WIFI_STATE_FAILED
} wifi_state_t;

static wifi_state_t s_state = WIFI_STATE_WAIT_TRIGGER;
static wifi_mode_t s_mode = WIFI_MODE_NONE;
static bool s_auth_triggered = false;
static uint32_t s_boot_time_ms = 0;
static wifi_cfg_file_t s_sta_cfg;
static bool s_has_sta_cfg = false;
static char s_current_ip_str[16] = "0.0.0.0";
static char s_current_ssid[33] = "";

static dhcp_server_t s_dhcp_server;
static dns_server_t s_dns_server;

static void load_sta_config(void)
{
    s_has_sta_cfg = false;
    memset(&s_sta_cfg, 0, sizeof(s_sta_cfg));

    lfs_t *lfs = lfs_port_get_fs();
    if (!lfs)
        return;

    lfs_file_t file;
    int err = lfs_file_open(lfs, &file, WIFI_CFG_PATH, LFS_O_RDONLY);
    if (err == LFS_ERR_OK)
    {
        lfs_ssize_t bytes = lfs_file_read(lfs, &file, &s_sta_cfg, sizeof(s_sta_cfg));
        lfs_file_close(lfs, &file);

        if (bytes == sizeof(s_sta_cfg) && s_sta_cfg.magic == WIFI_CFG_MAGIC)
        {
            s_has_sta_cfg = true;
            log_printf("[WiFi] Found saved Wi-Fi credentials for: %s\n", s_sta_cfg.ssid);
        }
    }
}

static void start_softap(void)
{
    log_printf("[WiFi] Starting SoftAP mode (SSID: %s)...\n", WIFI_AP_SSID_DEFAULT);

    cyw43_arch_enable_ap_mode(WIFI_AP_SSID_DEFAULT, NULL, CYW43_AUTH_OPEN);

    ip4_addr_t mask, gw;
    gw.addr = PP_HTONL(CYW43_DEFAULT_IP_AP_ADDRESS);
    mask.addr = PP_HTONL(CYW43_DEFAULT_IP_MASK);

    snprintf(s_current_ip_str, sizeof(s_current_ip_str), "192.168.4.1");
    strncpy(s_current_ssid, WIFI_AP_SSID_DEFAULT, sizeof(s_current_ssid) - 1);

    dhcp_server_init(&s_dhcp_server, &cyw43_state.netif[CYW43_ITF_AP], &gw, &mask);
    dns_server_init(&s_dns_server, &cyw43_state.netif[CYW43_ITF_AP], &gw);

    s_mode = WIFI_MODE_AP;
    s_state = WIFI_STATE_AP_ACTIVE;

    log_printf("[WiFi] SoftAP active! IP: %s, Captive Portal enabled\n", s_current_ip_str);
    web_server_init();
}

void wifi_manager_init(void)
{
    s_state = WIFI_STATE_WAIT_TRIGGER;
    s_mode = WIFI_MODE_NONE;
    s_auth_triggered = false;
    s_boot_time_ms = to_ms_since_boot(get_absolute_time());
    load_sta_config();

    log_printf("[WiFi] Manager initialized. Waiting for Xbox 360 auth or %d ms timeout...\n",
               WIFI_STARTUP_DELAY_MS);
}

void wifi_manager_trigger_auth_success(void)
{
    if (s_state == WIFI_STATE_WAIT_TRIGGER)
    {
        s_auth_triggered = true;
        log_printf("[WiFi] Auth success event received -> activating network stack\n");
    }
}

void wifi_manager_task(void)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());

    switch (s_state)
    {
    case WIFI_STATE_WAIT_TRIGGER:
    {
        if (s_auth_triggered || (now - s_boot_time_ms >= WIFI_STARTUP_DELAY_MS))
        {
            log_printf("[WiFi] Initializing CYW43 Wi-Fi chip...\n");
            if (cyw43_arch_init() != 0)
            {
                log_printf("[WiFi] Error: CYW43 init failed!\n");
                s_state = WIFI_STATE_FAILED;
                return;
            }
            s_state = WIFI_STATE_INIT;
        }
        break;
    }

    case WIFI_STATE_INIT:
    {
        if (s_has_sta_cfg && strlen(s_sta_cfg.ssid) > 0)
        {
            log_printf("[WiFi] Attempting to connect to home network: %s\n", s_sta_cfg.ssid);
            cyw43_arch_enable_sta_mode();
            s_state = WIFI_STATE_CONNECTING_STA;

            int res = cyw43_arch_wifi_connect_timeout_ms(
                s_sta_cfg.ssid,
                s_sta_cfg.password,
                CYW43_AUTH_WPA2_AES_PSK,
                8000);

            if (res == 0)
            {
                s_mode = WIFI_MODE_STA;
                s_state = WIFI_STATE_STA_CONNECTED;
                strncpy(s_current_ssid, s_sta_cfg.ssid, sizeof(s_current_ssid) - 1);
                struct netif *n = &cyw43_state.netif[CYW43_ITF_STA];
                ip4_addr_t ip = *netif_ip4_addr(n);
                snprintf(s_current_ip_str, sizeof(s_current_ip_str), "%d.%d.%d.%d",
                         ip4_addr1(&ip), ip4_addr2(&ip), ip4_addr3(&ip), ip4_addr4(&ip));
                log_printf("[WiFi] Connected to %s! Assigned IP: %s\n", s_current_ssid, s_current_ip_str);
                web_server_init();
            }
            else
            {
                log_printf("[WiFi] Connection to %s failed (%d). Falling back to SoftAP...\n",
                           s_sta_cfg.ssid, res);
                cyw43_arch_disable_sta_mode();
                start_softap();
            }
        }
        else
        {
            start_softap();
        }
        break;
    }

    case WIFI_STATE_STA_CONNECTED:
    case WIFI_STATE_AP_ACTIVE:
    case WIFI_STATE_FAILED:
    default:
        break;
    }
}

wifi_status_t wifi_manager_get_status(void)
{
    wifi_status_t status;
    status.mode = s_mode;
    strncpy(status.ssid, s_current_ssid, sizeof(status.ssid) - 1);
    status.ssid[sizeof(status.ssid) - 1] = '\0';
    strncpy(status.ip_str, s_current_ip_str, sizeof(status.ip_str) - 1);
    status.ip_str[sizeof(status.ip_str) - 1] = '\0';
    status.is_connected = (s_state == WIFI_STATE_AP_ACTIVE || s_state == WIFI_STATE_STA_CONNECTED);
    return status;
}

bool wifi_manager_set_sta_config(const char *ssid, const char *password)
{
    if (!ssid || strlen(ssid) == 0)
        return false;

    lfs_t *lfs = lfs_port_get_fs();
    if (!lfs)
        return false;

    wifi_cfg_file_t cfg;
    cfg.magic = WIFI_CFG_MAGIC;
    strncpy(cfg.ssid, ssid, sizeof(cfg.ssid) - 1);
    cfg.ssid[sizeof(cfg.ssid) - 1] = '\0';

    if (password)
    {
        strncpy(cfg.password, password, sizeof(cfg.password) - 1);
        cfg.password[sizeof(cfg.password) - 1] = '\0';
    }
    else
    {
        cfg.password[0] = '\0';
    }

    lfs_file_t file;
    int err = lfs_file_open(lfs, &file, WIFI_CFG_PATH, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (err != LFS_ERR_OK)
    {
        log_printf("[WiFi] Failed to open %s for writing: %d\n", WIFI_CFG_PATH, err);
        return false;
    }

    lfs_file_write(lfs, &file, &cfg, sizeof(cfg));
    lfs_file_close(lfs, &file);

    s_sta_cfg = cfg;
    s_has_sta_cfg = true;
    log_printf("[WiFi] Saved new Wi-Fi credentials for: %s\n", cfg.ssid);
    return true;
}

bool wifi_manager_clear_sta_config(void)
{
    lfs_t *lfs = lfs_port_get_fs();
    if (!lfs)
        return false;

    lfs_remove(lfs, WIFI_CFG_PATH);
    s_has_sta_cfg = false;
    memset(&s_sta_cfg, 0, sizeof(s_sta_cfg));
    log_printf("[WiFi] Cleared Wi-Fi credentials\n");
    return true;
}
