#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include "crash_info.h"

/* 공통 약속 1: [길이 4바이트, htonl][종류 1바이트][본문 길이만큼] */

enum msg_type {
    MSG_ANALYZE = 1,   /* 클라 -> 서버: 크래시 정보 */
    MSG_BUILD   = 2,   /* 클라 -> 서버: gcc 에러 텍스트 */
    MSG_HISTORY = 3,   /* 클라 -> 서버: "n=10" */
    MSG_SHOW    = 4,   /* 클라 -> 서버: "id=12" */
    MSG_RESULT  = 10,  /* 서버 -> 클라: 화면에 그대로 출력할 텍스트 */
    MSG_ERROR   = 11   /* 서버 -> 클라: 에러 사유 한 줄 */
};

#define MSG_MAX_BODY (64 * 1024)
#define MSG_BUILD_SOURCE_MARKER "\n\n---CRASHDOC-SOURCE---\n"

/* 메시지 하나를 끝까지 보낸다. 성공 0, 실패 -1 */
int send_msg(int fd, uint8_t type, const char *body, uint32_t len);

/* 메시지 하나를 끝까지 받는다. body는 호출자 버퍼(bodysz 크기),
 * 본문은 '\0'으로 끝나게 넣어 준다. 성공 시 본문 길이, 실패 -1 */
int recv_msg(int fd, uint8_t *type, char *body, uint32_t bodysz);

/* crash_info <-> ANALYZE 본문 ("키=값" 줄들 + 빈 줄 + 마지막 출력) */
int crash_info_encode(const crash_info *in, char *out, size_t outsz);
int crash_info_decode(const char *body, crash_info *out);

#endif
