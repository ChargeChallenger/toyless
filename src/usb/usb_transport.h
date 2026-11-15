#ifndef TOYLESS_USB_TRANSPORT_H
#define TOYLESS_USB_TRANSPORT_H

#include <stdint.h>
#include <stdbool.h>

void usb_transport_init(void);
void usb_transport_task(void);

bool usb_transport_send(const uint8_t *packet32);

#endif