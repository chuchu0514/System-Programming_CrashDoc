#include <stdlib.h>

int main(void)
{
    void *pointer = malloc(16);
    void (*volatile release)(void *) = free;

    if (pointer == NULL)
        return 1;

    /* 같은 주소를 두 번 해제해 glibc의 SIGABRT를 재현한다. */
    release(pointer);
    release(pointer);
    return 0;
}
