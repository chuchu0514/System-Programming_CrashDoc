/* 담당: 김경민 — 크래시 기록 저장·조회 */
#include "store.h"

int store_save(const crash_info *in, const char *result, const char *client_ip)
{
    (void)in; (void)result; (void)client_ip;
    return -1; /* TODO */
}

int store_list(int n, char *out, size_t outsz)
{
    (void)n; (void)out; (void)outsz;
    return -1; /* TODO */
}

int store_show(int id, char *out, size_t outsz)
{
    (void)id; (void)out; (void)outsz;
    return -1; /* TODO */
}
