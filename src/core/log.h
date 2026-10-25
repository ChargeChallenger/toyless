#ifndef TOYLESS_LOG_H
#define TOYLESS_LOG_H

#include <stdint.h>
#include <stdarg.h>

void log_init(void);
void log_printf(const char *fmt, ...);
void log_flush_uart(void);

#endif 