#ifndef INFINITY_PROTOCOL_H
#define INFINITY_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Packet and Data Sizes
#define INFINITY_PACKET_SIZE      32
#define INFINITY_FIGURE_SIZE      320
#define INFINITY_NUM_SLOTS        9
#define INFINITY_SLOTS_PER_PAD    3
#define INFINITY_PAD_COUNT        3

// Xbox 360 Input Packet Wrapper Header 
#define INFINITY_WRAPPER_BYTE0    0x0B
#define INFINITY_WRAPPER_BYTE1    0x15
#define INFINITY_WRAPPER_LEN      2

// Packet type prefixes
#define INFINITY_PREFIX_CMD       0xFF
#define INFINITY_PREFIX_RESP      0xAA
#define INFINITY_PREFIX_NOTIFY    0xAB

// Command Opcodes
#define INFINITY_CMD_ACTIVATE     0x80
#define INFINITY_CMD_PRNG_SEED    0x81
#define INFINITY_CMD_PRNG_NEXT    0x83
#define INFINITY_CMD_LED_FADE     0x90
#define INFINITY_CMD_LED_COLOR    0x92
#define INFINITY_CMD_LED_FLASH    0x93
#define INFINITY_CMD_LED_SPECIAL1 0x95
#define INFINITY_CMD_LED_SPECIAL2 0x96
#define INFINITY_CMD_GET_PRESENT  0xA1
#define INFINITY_CMD_READ_BLOCK   0xA2
#define INFINITY_CMD_WRITE_BLOCK  0xA3
#define INFINITY_CMD_GET_UID      0xB4
#define INFINITY_CMD_STATUS       0xB5

// Pad identification numbers (in 0xAB notification)
#define INFINITY_PAD_HEX          1
#define INFINITY_PAD_P1           2
#define INFINITY_PAD_P2           3

// Base slot codes (in 0xA1 presense response)
#define INFINITY_BASE_HEX         0x10
#define INFINITY_BASE_P1          0x20
#define INFINITY_BASE_P2          0x30

// Figure placement events (in 0xAB notifications)
#define INFINITY_EVENT_ADDED      0x00
#define INFINITY_EVENT_REMOVED    0x01

#endif
