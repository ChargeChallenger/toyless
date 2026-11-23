#include <string.h>
#include "xsm3_service.h"
#include "usb/usb_descriptors.h"
#include "core/icc.h"
#include "core/log.h"
#include "xsm3.h"

// Capabilities structures for XInput
typedef struct TU_ATTR_PACKED {
    uint8_t  report_id;
    uint8_t  report_size;
    uint8_t  padding;
    uint8_t  left_motor;
    uint8_t  right_motor;
    uint8_t  padding2[3];
} xinput_vibration_caps_t;

typedef struct TU_ATTR_PACKED {
    uint8_t  report_id;
    uint8_t  report_size;
    uint16_t buttons;
    uint8_t  left_trigger;
    uint8_t  right_trigger;
    uint16_t left_thumb_x;
    uint16_t left_thumb_y;
    uint16_t right_thumb_x;
    uint16_t right_thumb_y;
    uint8_t  reserved[4];
    uint16_t flags;
} xinput_input_caps_t;

_Static_assert(sizeof(xinput_vibration_caps_t) == 8, "Vibration caps must be 8 bytes");
_Static_assert(sizeof(xinput_input_caps_t) == 20, "Input caps must be 20 bytes");

static const xinput_vibration_caps_t xinput_vibration_caps = {
    .report_size = sizeof(xinput_vibration_caps_t),
};

static const xinput_input_caps_t xinput_input_caps = {
    .report_size = sizeof(xinput_input_caps_t),
};

// Authentication state (2 bytes): 1 = in progress, 2 = authenticated/completed
static const uint16_t xsm3_auth_state = 2;
_Static_assert(sizeof(xsm3_auth_state) == 2, "XSM3 state must be 2 bytes");

// Static persistent buffer for serial binary bytes (TinyUSB holds buffer pointer during xfer)
static uint8_t s_serial_buf[4] __attribute__((aligned(4)));

// Buffer for receiving incoming challenge payload from host (OUT requests 0x82, 0x87, 0x00)
static uint8_t xsm3_rx_buf[64] __attribute__((aligned(4)));

static volatile bool s_authenticated = false;

void xsm3_service_init(void) {
    s_authenticated = false;
    xsm3_initialise_state();
    xsm3_set_identification_data(xsm3_id_data_ms_controller);
    log_printf("[XSM3] Service initialized\n");
}

bool xsm3_service_is_authenticated(void) {
    return s_authenticated;
}

bool xsm3_service_handle_control_xfer(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
    if (stage == CONTROL_STAGE_SETUP) {
        log_printf("[XSM3-CTRL] req=0x%02X bmType=0x%02X wVal=0x%04X wIdx=0x%04X wLen=%u\n",
                   request->bRequest, request->bmRequestType, request->wValue, request->wIndex, request->wLength);

        // 1. GET_REPORT (0x01)
        if (request->bRequest == 0x01) {
            if (request->bmRequestType == 0xC0 && request->wValue == 0x0000) {
                usb_descriptors_get_serial_bytes(s_serial_buf);
                return tud_control_xfer(rhport, request, s_serial_buf, sizeof(s_serial_buf));
            }
            if (request->bmRequestType == 0xC1 && request->wValue == 0x0100) {
                return tud_control_xfer(rhport, request, (void *)&xinput_input_caps, sizeof(xinput_input_caps));
            }
            if (request->bmRequestType == 0xC1 && request->wValue == 0x0000) {
                return tud_control_xfer(rhport, request, (void *)&xinput_vibration_caps, sizeof(xinput_vibration_caps));
            }
            return false;
        }

        // 2. XSM3 Authentication & Vendor Control Requests
        switch (request->bRequest) {
            case 0x81: // IN: 29 bytes identification packet
                s_authenticated = false;
                xsm3_initialise_state();
                xsm3_set_identification_data(xsm3_id_data_ms_controller);
                log_printf("[XSM3] Sending 0x81 Identification\n");
                return tud_control_xfer(rhport, request, (void *)xsm3_id_data_ms_controller, sizeof(xsm3_id_data_ms_controller));

            case 0x82: // OUT: Challenge init (34 bytes from host)
                if (request->wLength != 0x22) {
                    log_printf("[XSM3] 0x82 unexpected wLength: %u\n", request->wLength);
                    return false;
                }
                log_printf("[XSM3] Receiving 0x82 Challenge init\n");
                return tud_control_xfer(rhport, request, xsm3_rx_buf, sizeof(xsm3_rx_buf));

            case 0x83: // IN: Challenge response (48 bytes)
                log_printf("[XSM3] Sending 0x83 Challenge response\n");
                return tud_control_xfer(rhport, request, xsm3_challenge_response, sizeof(xsm3_challenge_response));

            case 0x84: // Status ACK without data
                return tud_control_status(rhport, request);

            case 0x86: // IN: Authentication state (2 bytes, 0x0002)
                return tud_control_xfer(rhport, request, (void *)&xsm3_auth_state, sizeof(xsm3_auth_state));

            case 0x87: // OUT: Challenge verify (22 bytes from host)
                if (request->wLength != 0x16) {
                    log_printf("[XSM3] 0x87 unexpected wLength: %u\n", request->wLength);
                    return false;
                }
                log_printf("[XSM3] Receiving 0x87 Challenge verify\n");
                return tud_control_xfer(rhport, request, xsm3_rx_buf, sizeof(xsm3_rx_buf));

            case 0x00: // LED / vibration output report ACK
                if (request->wLength > 0) {
                    if (request->wLength > sizeof(xsm3_rx_buf)) return false;
                    return tud_control_xfer(rhport, request, xsm3_rx_buf, sizeof(xsm3_rx_buf));
                }
                return tud_control_status(rhport, request);

            default:
                log_printf("[XSM3] Unhandled vendor request: 0x%02X\n", request->bRequest);
                return false;
        }
    } else if (stage == CONTROL_STAGE_DATA) {
        if (request->bRequest == 0x82) {
            if (!xsm3_do_challenge_init(xsm3_rx_buf)) {
                log_printf("[XSM3] Challenge init validation failed!\n");
                return false;
            }
        } else if (request->bRequest == 0x87) {
            if (!xsm3_do_challenge_verify(xsm3_rx_buf)) {
                log_printf("[XSM3] Challenge verify validation failed!\n");
                return false;
            }
            s_authenticated = true;
            portal_event_t evt = {
                .type = PORTAL_EVT_AUTH_SUCCESS,
                .slot = 0,
                .block_index = 0,
            };
            icc_send_event(&evt);
            log_printf("[XSM3] Authentication handshake completed successfully!\n");
        }
    }

    return true;
}
