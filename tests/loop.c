int main(void)
{
    volatile unsigned long count = 0;

    /* 외부에서 종료할 때까지 실행해 제한 시간 동작을 검증한다. */
    for (;;)
        ++count;
}
