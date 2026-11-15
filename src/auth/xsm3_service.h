#ifndef TOYLESS_XSM3_SERVICE_H
#define TOYLESS_XSM3_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "tusb.h"

void xsm3_service_init(void);

bool xsm3_service_is_authenticated(void);

bool xsm3_service_handle_control_xfer(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request);

#endif // TOYLESS_XSM3_SERVICE_H
