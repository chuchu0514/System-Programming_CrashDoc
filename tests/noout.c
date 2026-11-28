#include <signal.h>

int main(void)
{
    /* 크래시 전에 아무것도 출력하지 않는다. */
    raise(SIGSEGV);
    return 0;
}
