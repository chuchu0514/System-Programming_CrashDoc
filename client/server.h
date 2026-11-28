#ifndef CRASHDOC_CLIENT_SERVER_H
#define CRASHDOC_CLIENT_SERVER_H

#include <stdint.h>

struct server_address {
    char host[256];
    char port[6];
};

int parse_server_address(const char *text, struct server_address *address);
int request_server(const struct server_address *address, uint8_t request,
                   const char *body, uint32_t length);
void request_analysis(const struct server_address *address,
                      const char *body, uint32_t length);

#endif
