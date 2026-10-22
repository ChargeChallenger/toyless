#include "ipc.h"
#include "pico/util/queue.h"

#define CMD_QUEUE_LEN 8
#define EVT_QUEUE_LEN 16

static queue_t cmd_queue;
static queue_t evt_queue;

void ipc_init(void)
{
    queue_init(&cmd_queue, sizeof(portal_cmd_t), CMD_QUEUE_LEN);
    queue_init(&evt_queue, sizeof(portal_event_t), EVT_QUEUE_LEN);
}

bool ipc_send_cmd(const portal_cmd_t *cmd)
{
    return queue_try_add(&cmd_queue, (void *)cmd);
}

bool ipc_poll_cmd(portal_cmd_t *out_cmd)
{
    return queue_try_remove(&cmd_queue, (void *)out_cmd);
}

bool ipc_send_event(const portal_event_t *evt)
{
    return queue_try_add(&evt_queue, (void *)evt);
}

bool ipc_poll_event(portal_event_t *out_evt)
{
    return queue_try_remove(&evt_queue, (void *)out_evt);
}
