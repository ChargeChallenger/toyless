#ifndef TOYLESS_IPC_H
#define TOYLESS_IPC_H

#include <stdint.h>
#include <stdbool.h>

#define INFINITY_FIGURE_SIZE 320

typedef enum {
    PORTAL_CMD_SET_FIGURE,
    PORTAL_CMD_REMOVE_FIGURE,
    PORTAL_CMD_RESET_FIGURE
} portal_cmd_type_t;

typedef struct {
    portal_cmd_type_t type;
    uint8_t slot;
    uint8_t figure_data[INFINITY_FIGURE_SIZE];
} portal_cmd_t;

typedef enum {
    PORTAL_EVT_FIGURE_DIRTY,
    PORTAL_EVT_STATUS_CHANGE
} portal_event_type_t;

typedef struct {
    portal_event_type_t type;
    uint8_t slot;
    uint8_t block_index;
    uint8_t block_data[16];
} portal_event_t;

void ipc_init(void);

bool ipc_send_cmd(const portal_cmd_t *cmd);
bool ipc_poll_cmd(portal_cmd_t *out_cmd);

bool ipc_send_event(const portal_event_t *evt);
bool ipc_poll_event(portal_event_t *out_evt);

#endif 