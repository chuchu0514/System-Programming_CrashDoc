/* 담당: 추진성 — crashdoc-server 진입점 */
#include <stdio.h>
#include "analyzer.h"
#include "store.h"

int main(int argc, char *argv[])
{
    (void)argv;
    if (argc < 2) {
        fprintf(stderr, "사용법: crashdoc-server <포트> [--analyzer rule|api|local]\n");
        return 1;
    }
    /* TODO: socket / bind / listen / accept 루프, 연결마다 스레드 */
    return 0;
}
