#include <string.h>
#include "tusb.h"
#include "pico/unique_id.h"
#include "usb_descriptors.h"

// ---------------------------------------------------------------------------
// 1. Device Descriptor
// ---------------------------------------------------------------------------
static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0xFF,   // Vendor Specific
    .bDeviceSubClass    = 0xFF,
    .bDeviceProtocol    = 0xFF,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE, // 8 bytes
    .idVendor           = PORTAL_USB_VID,         // 0x24C6 (PDP)
    .idProduct          = PORTAL_USB_PID,         // 0xFA00
    .bcdDevice          = PORTAL_USB_REV,         // 0x0100
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

// ---------------------------------------------------------------------------
// 2. Configuration Descriptor (4 interfaces, exactly 153 bytes)
// ---------------------------------------------------------------------------
#define CONFIG_TOTAL_LEN 153

static const uint8_t desc_configuration[] = {
    // === Configuration Descriptor (9 bytes) ===
    0x09, TUSB_DESC_CONFIGURATION,
    U16_TO_U8S_LE(CONFIG_TOTAL_LEN),
    0x04,        // bNumInterfaces = 4
    0x01,        // bConfigurationValue = 1
    0x00,        // iConfiguration
    0x80,        // bmAttributes: Bus-Powered
    0xFA,        // bMaxPower = 500 mA (250 * 2 mA)

    // === Interface 0: Disney Infinity Base Data (9 bytes) ===
    0x09, TUSB_DESC_INTERFACE,
    0x00, 0x00, 0x02, 0xFF, 0x5D, 0x01, 0x00,

    // Vendor Sub-Descriptor XInput (Type 0x21, 17 bytes)
    0x11, 0x21, 0x10, 0x01, 0x24, 0x25,
    EP_PORTAL_IN, 0x14,
    0x03, 0x03, 0x03, 0x04, 0x13,
    EP_PORTAL_OUT, 0x08,
    0x03, 0x03,

    // EP 0x81 IN (Interrupt, 32 bytes, 4 ms interval)
    0x07, TUSB_DESC_ENDPOINT, EP_PORTAL_IN, TUSB_XFER_INTERRUPT,
    U16_TO_U8S_LE(EP_PORTAL_SIZE), 0x04,
    // EP 0x02 OUT (Interrupt, 32 bytes, 4 ms interval)
    0x07, TUSB_DESC_ENDPOINT, EP_PORTAL_OUT, TUSB_XFER_INTERRUPT,
    U16_TO_U8S_LE(EP_PORTAL_SIZE), 0x04,

    // === Interface 1: Xbox Audio Subsystem (9 bytes) ===
    0x09, TUSB_DESC_INTERFACE,
    0x01, 0x00, 0x04, 0xFF, 0x5D, 0x03, 0x00,
    // Audio Vendor Sub-Descriptor (Type 0x21, 27 bytes)
    0x1B, 0x21, 0x00, 0x01, 0x01, 0x01, 0x83, 0x40, 0x01, 0x04,
    0x20, 0x16, 0x85, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16,
    0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 4 Audio Endpoints
    0x07, TUSB_DESC_ENDPOINT, 0x83, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(32), 0x02,
    0x07, TUSB_DESC_ENDPOINT, 0x04, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(32), 0x04,
    0x07, TUSB_DESC_ENDPOINT, 0x85, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(32), 0x40,
    0x07, TUSB_DESC_ENDPOINT, 0x05, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(32), 0x10,

    // === Interface 2: Plugin Module (9 bytes) ===
    0x09, TUSB_DESC_INTERFACE,
    0x02, 0x00, 0x01, 0xFF, 0x5D, 0x02, 0x00,
    // Plugin Vendor Sub-Descriptor (Type 0x21, 9 bytes)
    0x09, 0x21, 0x00, 0x01, 0x01, 0x22, 0x86, 0x07, 0x00,
    // EP 0x86 IN
    0x07, TUSB_DESC_ENDPOINT, 0x86, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(32), 0x10,

    // === Interface 3: XSM3 Security (9 bytes, iInterface = 4) ===
    0x09, TUSB_DESC_INTERFACE,
    0x03, 0x00, 0x00, 0xFF, 0xFD, 0x13, 0x04,
    // Security Vendor Sub-Descriptor (Type 0x41, 6 bytes)
    0x06, 0x41, 0x00, 0x01, 0x01, 0x03,
};

_Static_assert(sizeof(desc_configuration) == CONFIG_TOTAL_LEN,
               "Configuration descriptor total length must be exactly 153 bytes");

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

// ---------------------------------------------------------------------------
// 3. String Descriptors
// ---------------------------------------------------------------------------
static const char *const string_desc_arr[] = {
    NULL,                   // 0: Language (0x0409)
    "Disney",               // 1: Manufacturer
    "Disney Infinity Base", // 2: Product
};

static uint16_t desc_str_buf[95];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    uint8_t len = 0;

    if (index == 0) {
        desc_str_buf[1] = 0x0409; // English (United States)
        len = 1;
    } else if (index == 3) {
        // Serial number derived from unique RP2040 chip ID
        pico_unique_board_id_t id;
        pico_get_unique_board_id(&id);
        char serial[17];
        for (int i = 0; i < 8; i++) {
            snprintf(serial + (i * 2), 3, "%02X", id.id[i]);
        }
        for (len = 0; len < 16; len++) {
            desc_str_buf[1 + len] = (uint16_t)serial[len];
        }
    } else if (index == 4) {
        // Mandatory XSM3 security descriptor string with copyright symbol U+00A9
        static const char prefix[] = "Xbox Security Method 3, Version 1.00, ";
        static const char suffix[] = " 2005 Microsoft Corporation. All rights reserved.";
        int p = 1;
        for (size_t i = 0; i < strlen(prefix); i++) desc_str_buf[p++] = prefix[i];
        desc_str_buf[p++] = 0x00A9; // Copyright symbol (C)
        for (size_t i = 0; i < strlen(suffix); i++) desc_str_buf[p++] = suffix[i];
        len = (uint8_t)(p - 1);
    } else if (index < 3) {
        const char *str = string_desc_arr[index];
        len = (uint8_t)strlen(str);
        for (uint8_t i = 0; i < len; i++) desc_str_buf[1 + i] = str[i];
    } else {
        return NULL;
    }

    // First byte is length, second byte is descriptor type (String = 3)
    desc_str_buf[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * len + 2));
    return desc_str_buf;
}
