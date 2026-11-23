#ifndef TOYLESS_WEB_SERVER_H
#define TOYLESS_WEB_SERVER_H

#include <stdbool.h>
#include <stdint.h>

#define WEB_SERVER_PORT 80

bool web_server_init(void);

void web_server_deinit(void);

#endif