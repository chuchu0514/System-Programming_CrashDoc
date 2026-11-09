/* 담당: 추진성 (11/15까지) */
#include "protocol.h"

int send_msg(int fd, uint8_t type, const char *body, uint32_t len)
{
    (void)fd; (void)type; (void)body; (void)len;
    return -1; /* TODO */
}

int recv_msg(int fd, uint8_t *type, char *body, uint32_t bodysz)
{
    (void)fd; (void)type; (void)body; (void)bodysz;
    return -1; /* TODO */
}

int crash_info_encode(const crash_info *in, char *out, size_t outsz)
{
    (void)in; (void)out; (void)outsz;
    return -1; /* TODO */
}

int crash_info_decode(const char *body, crash_info *out)
{
    (void)body; (void)out;
    return -1; /* TODO */
}
