#ifndef STORE_H
#define STORE_H

#include <stddef.h>
#include "crash_info.h"

/* 담당: 김경민. 공통 약속 2.
 * 내부에서 mutex로 보호한다 (여러 스레드가 동시에 호출). */
int store_save(const crash_info *in, const char *result, const char *client_ip); /* 새 기록 번호, 실패 -1 */
int store_list(int n, char *out, size_t outsz);   /* 최근 n개 목록 텍스트 */
int store_show(int id, char *out, size_t outsz);  /* 기록 하나 자세히 */

#endif
