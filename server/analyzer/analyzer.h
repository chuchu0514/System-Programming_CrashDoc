#ifndef ANALYZER_H
#define ANALYZER_H

#include <stddef.h>
#include "crash_info.h"

/* 담당: 김경민. 공통 약속 2.
 * 여러 스레드가 동시에 부르므로 static/전역 버퍼 금지.
 * out에 "원인 추정: ...\n점검: ...\n" 형태로 쓴다. 성공 0, 실패 -1
 * printf로 화면에 쓰지 말 것 (대시보드가 깨짐) */
int analyze_crash(const crash_info *in, char *out, size_t outsz);
int analyze_build(const char *error_text, char *out, size_t outsz);

#endif
