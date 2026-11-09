/* 담당: 최완호 — crashdoc 클라이언트 진입점 */
#include <stdio.h>

static void usage(void)
{
    fprintf(stderr,
        "사용법:\n"
        "  crashdoc run [--timeout 초] [--server 호스트:포트] <프로그램> [인자...]\n"
        "  crashdoc build gcc <인자...>\n"
        "  crashdoc history [-n 개수]\n"
        "  crashdoc show <번호>\n");
}

int main(int argc, char *argv[])
{
    (void)argv;
    if (argc < 2) {
        usage();
        return 1;
    }
    /* TODO: argv[1]에 따라 run / build / history / show 로 나누기 */
    usage();
    return 1;
}
