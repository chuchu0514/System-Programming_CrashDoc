int main(void)
{
    volatile int dividend = 42;
    volatile int divisor = 0;

    /* 정수 나눗셈을 실제로 실행해 SIGFPE를 재현한다. */
    return dividend / divisor;
}
