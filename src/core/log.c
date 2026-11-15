#include "log.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"

#define LOG_BUFFER_SIZE 4096

static char log_buffer[LOG_BUFFER_SIZE];
static volatile uint16_t head = 0;
static volatile uint16_t tail = 0;
static spin_lock_t *log_spinlock;

void log_init(void) {
    log_spinlock = spin_lock_init(spin_lock_claim_unused(true));
}

void log_printf(const char *fmt, ...) {
    char buf[128];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len <= 0) return;

    uint32_t save = spin_lock_blocking(log_spinlock);
    uint16_t cur_head = head;
    for (int i = 0; i < len; i++) {
        uint16_t next = (cur_head + 1) % LOG_BUFFER_SIZE;
        if (next == tail) break;
        log_buffer[cur_head] = buf[i];
        cur_head = next;
    }
    head = cur_head;
    spin_unlock(log_spinlock, save);
}

void log_flush_uart(void) {
    while (tail != head) {
        putchar(log_buffer[tail]);
        tail = (tail + 1) % LOG_BUFFER_SIZE;
    }
}