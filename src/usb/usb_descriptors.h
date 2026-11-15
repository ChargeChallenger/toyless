#ifndef TOYLESS_USB_DESCRIPTORS_H
#define TOYLESS_USB_DESCRIPTORS_H

#include <stdlib.h>
#include "tusb.h"

// Аппаратные идентификаторы оригинальной базы Disney Infinity для Xbox 360
#define PORTAL_USB_VID  0x24C6 
#define PORTAL_USB_PID  0xFA00
#define PORTAL_USB_REV  0x0100

// Эндпоинты основного интерфейса данных базы
#define EP_PORTAL_IN    0x81
#define EP_PORTAL_OUT   0x02
#define EP_PORTAL_SIZE  32

#endif