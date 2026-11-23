#include <string.h>
#include "pico/stdlib.h"
#include "usb_transport.h"
#include "usb_descriptors.h"
#include "auth/xsm3_service.h"
#include "protocol/infinity_protocol.h"
#include "protocol/infinity_engine.h"
#include "core/log.h"
#include "tusb.h"
#include "device/usbd_pvt.h"

// Buffer for receiving incoming commands from host (EP 0x02 OUT)
static uint8_t rx_buf[EP_PORTAL_SIZE];

// Ring buffer FIFO for outgoing replies (EP 0x81 IN)
#define TX_FIFO_SIZE 8
static uint8_t tx_fifo[TX_FIFO_SIZE][EP_PORTAL_SIZE];
static volatile int tx_head = 0;
static volatile int tx_tail = 0;
static volatile bool tx_in_flight = false;
static uint8_t active_rhport = 0;

static void tx_pump(uint8_t rhport) {
    if (tx_in_flight || tx_head == tx_tail) return;
    tx_in_flight = true;
    usbd_edpt_xfer(rhport, EP_PORTAL_IN, tx_fifo[tx_head], EP_PORTAL_SIZE);
}

static uint32_t notif_pending_since_ms = 0;

static void check_proactive_notifications(void) {
    if (!infinity_notification_pending()) {
        notif_pending_since_ms = 0;
        return;
    }

    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (notif_pending_since_ms == 0) {
        notif_pending_since_ms = now;
        return;
    }

    // Host has not polled for >300ms -> proactively push notifications
    if ((now - notif_pending_since_ms) < 300) return;

    // Do not pop if the TX FIFO is full
    if (((tx_tail + 1) % TX_FIFO_SIZE) == tx_head) return;

    uint8_t pkt[EP_PORTAL_SIZE];
    memset(pkt, 0, sizeof(pkt));
    int len = infinity_pop_notification(pkt + 2);
    if (len > 0) {
        pkt[0] = INFINITY_WRAPPER_BYTE0;
        pkt[1] = INFINITY_WRAPPER_BYTE1;
        if (usb_transport_send(pkt)) {
            notif_pending_since_ms = 0;
        }
    }
}

bool usb_transport_send(const uint8_t *packet32) {
    int next = (tx_tail + 1) % TX_FIFO_SIZE;
    if (next == tx_head) {
        log_printf("[USB] TX FIFO full, packet dropped\n");
        return false;
    }
    memcpy(tx_fifo[tx_tail], packet32, EP_PORTAL_SIZE);
    tx_tail = next;
    tx_pump(active_rhport);
    return true;
}

// ---------------------------------------------------------------------------
// TinyUSB Callbacks
// ---------------------------------------------------------------------------

static void xinput_init(void) {}
static void xinput_reset(uint8_t rhport) {
    (void)rhport;
    xsm3_service_init();
    notif_pending_since_ms = 0;
}

static uint16_t xinput_open(uint8_t rhport, tusb_desc_interface_t const *desc_intf, uint16_t max_len) {
    TU_VERIFY(desc_intf->bInterfaceClass == 0xFF &&
              desc_intf->bInterfaceSubClass == 0x5D &&
              desc_intf->bInterfaceNumber == 0);

    active_rhport = rhport;
    log_printf("[USB] Host configured base interfaces\n");

    // Open all endpoints across all 4 interfaces
    uint8_t const *p = (uint8_t const *)desc_intf;
    uint8_t const *end = p + max_len;
    while (p < end) {
        uint8_t dlen = tu_desc_len(p);
        if (dlen == 0) break;
        if (tu_desc_type(p) == TUSB_DESC_ENDPOINT) {
            tusb_desc_endpoint_t const *ep = (tusb_desc_endpoint_t const *)p;
            usbd_edpt_open(rhport, ep);
            if (ep->bEndpointAddress == EP_PORTAL_OUT) {
                // Prime reception of incoming host commands
                usbd_edpt_xfer(rhport, EP_PORTAL_OUT, rx_buf, EP_PORTAL_SIZE);
            }
        }
        p += dlen;
    }
    return max_len;
}

static bool xinput_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
    (void)rhport;
    log_printf("[XINPUT-CTRL] stage=%d req=0x%02X type=0x%02X val=0x%04X idx=0x%04X len=%u\n",
               stage, request->bRequest, request->bmRequestType, request->wValue, request->wIndex, request->wLength);
    return false;
}

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
    return xsm3_service_handle_control_xfer(rhport, stage, request);
}

static bool xinput_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
    (void)result;

    if (ep_addr == EP_PORTAL_IN) {
        // Current IN packet delivered to host: advance FIFO and pump next
        tx_in_flight = false;
        tx_head = (tx_head + 1) % TX_FIFO_SIZE;
        tx_pump(rhport);
        return true;
    }

    if (ep_addr == EP_PORTAL_OUT) {
        log_printf("[PORTAL-OUT] %lu bytes: %02X %02X %02X %02X\n",
                   (unsigned long)xferred_bytes,
                   xferred_bytes > 0 ? rx_buf[0] : 0,
                   xferred_bytes > 1 ? rx_buf[1] : 0,
                   xferred_bytes > 2 ? rx_buf[2] : 0,
                   xferred_bytes > 3 ? rx_buf[3] : 0);

        // Verify Disney Infinity packet wrapper on Xbox 360 (0x0B 0x15)
        if (xferred_bytes >= 3 && rx_buf[0] == INFINITY_WRAPPER_BYTE0 && rx_buf[1] == INFINITY_WRAPPER_BYTE1) {
            const uint8_t *pkt = rx_buf + 2;
            uint8_t resp[EP_PORTAL_SIZE] = {0};
            int len = 0;

            if (pkt[0] == INFINITY_PREFIX_CMD) {
                // Game command 
                log_printf("[PORTAL] Game command 0x%02X (seq %d)\n", pkt[2], pkt[3]);
                len = infinity_process_command(pkt, resp + 2);
            } else {
                // Host polling 
                len = infinity_pop_notification(resp + 2);
            }

            if (len > 0) {
                resp[0] = INFINITY_WRAPPER_BYTE0;
                resp[1] = INFINITY_WRAPPER_BYTE1;
                log_printf("[PORTAL] Sending resp len=%d (type 0x%02X)\n", len, resp[2]);
                usb_transport_send(resp);
            }
        }
        else if (xferred_bytes > 0) {
            log_printf("[PORTAL-OUT] Unexpected wrapper: %02X %02X\n", rx_buf[0], rx_buf[1]);
        }

        // wait for next command from host
        usbd_edpt_xfer(rhport, EP_PORTAL_OUT, rx_buf, EP_PORTAL_SIZE);
    }
    return true;
}

static const usbd_class_driver_t xinput_class_driver = {
    .init            = xinput_init,
    .reset           = xinput_reset,
    .open            = xinput_open,
    .control_xfer_cb = xinput_control_xfer_cb,
    .xfer_cb         = xinput_xfer_cb,
    .sof             = NULL,
};

usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count) {
    *driver_count = 1;
    return &xinput_class_driver;
}

void usb_transport_init(void) {
    tusb_init();
}

void usb_transport_task(void) {
    tud_task();
    check_proactive_notifications();
}
