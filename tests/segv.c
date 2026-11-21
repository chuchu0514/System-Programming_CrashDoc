#include <stdio.h>

int main(void)
{
    volatile int *pointer = NULL;

    /* 크래시 직전의 두 줄이 기록에 남는지 확인한다. */
    puts("segv: 첫 번째 출력");
    puts("segv: 두 번째 출력");
    *pointer = 1;
    return 0;
}
