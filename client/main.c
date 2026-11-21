#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "run.h"

static int usage(void)
{
    fprintf(stderr, "사용법: crashdoc run [--server 호스트:포트] <프로그램> [인자...]\n");
    return 1;
}

int main(int argc, char *argv[])
{
    if (argc < 3 || strcmp(argv[1], "run") != 0)
        return usage();

    /* 접속 주소는 옵션, 환경변수, 기본값 순으로 선택한다. */
    int program = 2;
    const char *endpoint = getenv("CRASHDOC_SERVER");
    if (strcmp(argv[program], "--server") == 0) {
        if (argc < 5)
            return usage();
        endpoint = argv[3];
        program = 4;
    } else if (!endpoint || !*endpoint) {
        endpoint = "127.0.0.1:9000";
    }

    struct server_address address;
    if (parse_server_address(endpoint, &address) < 0) {
        fprintf(stderr, "서버 주소는 호스트:포트 형식이어야 합니다.\n");
        return 1;
    }
    return run_program(argv + program, &address);
}
