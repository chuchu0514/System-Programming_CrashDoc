#include <signal.h>
#include <stdio.h>

int main(void)
{
    /* 15줄 중 마지막 10줄(06~15)이 기록되는지 확인한다. */
    for (int line = 1; line <= 15; ++line)
        printf("many: %02d\n", line);
    fflush(stdout);
    raise(SIGSEGV);
    return 0;
}
