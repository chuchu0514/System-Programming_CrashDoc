int main(void)
{
#ifdef CRASHDOC_TEST_BUILD_ERROR
    /* 이 매크로를 켜면 선언하지 않은 변수로 GCC 진단을 재현한다. */
    return undeclared_variable;
#else
    return 0;
#endif
}
