#include <stdio.h>
#include <string.h>

#include "analyzer.h"

static int check_contains(const char *name, const char *text, const char *expected)
{
    if (strstr(text, expected) == NULL) {
        fprintf(stderr, "FAIL: %s\n  expected: %s\n  got: %s\n",
                name, expected, text);
        return 1;
    }
    printf("PASS: %s\n", name);
    return 0;
}

static int check_crash(const char *name, crash_info *in, const char *expected)
{
    char out[12288];

    if (analyze_crash(in, out, sizeof(out)) != 0) {
        fprintf(stderr, "FAIL: %s: analyze_crash returned an error\n", name);
        return 1;
    }
    return check_contains(name, out, expected);
}

static int check_build(const char *name, const char *error_text, const char *expected)
{
    char out[4096];

    if (analyze_build(error_text, out, sizeof(out)) != 0) {
        fprintf(stderr, "FAIL: %s: analyze_build returned an error\n", name);
        return 1;
    }
    return check_contains(name, out, expected);
}

int main(void)
{
    static const struct {
        int number;
        const char *name;
    } signals[] = {
        {11, "SIGSEGV(11)"},
        {8, "SIGFPE(8)"},
        {6, "SIGABRT(6)"},
        {7, "SIGBUS(7)"},
        {4, "SIGILL(4)"},
        {15, "SIGTERM(15)"},
        {2, "SIGINT(2)"},
        {9, "SIGKILL(9)"}
    };
    static const struct {
        const char *name;
        const char *message;
        const char *expected;
    } build_cases[] = {
        {"undeclared variable", "main.c:10:2: error: 'item' undeclared", "선언되지 않은"},
        {"missing semicolon", "main.c:11:2: error: expected ';' before return", "세미콜론"},
        {"implicit function declaration", "main.c:12:2: warning: implicit declaration of function 'run'", "함수 선언"},
        {"incompatible pointer type", "main.c:13:2: warning: incompatible pointer type", "포인터 자료형"},
        {"undefined reference", "main.c:14:2: undefined reference to 'run'", "링커"}
    };
    crash_info in;
    char out[12288];
    size_t i;
    int failures = 0;

    analyzer_init(0);
    memset(&in, 0, sizeof(in));
    in.elapsed_ms = 500;
    for (i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i) {
        in.signo = signals[i].number;
        failures += check_crash(signals[i].name, &in, signals[i].name);
    }

    in.signo = 0;
    in.timed_out = 1;
    if (analyze_crash(&in, out, sizeof(out)) != 0) {
        fprintf(stderr, "FAIL: timeout: analyze_crash returned an error\n");
        ++failures;
    } else {
        failures += check_contains("timeout", out, "시간 초과");
        failures += check_contains("timeout is described as uncertain", out,
                                   "무한 루프 또는 장시간 작업일 수 있습니다");
    }

    in.timed_out = 0;
    in.exit_code = 3;
    if (analyze_crash(&in, out, sizeof(out)) != 0)
        ++failures;
    else {
        failures += check_contains("direct exit code", out, "종료 코드 오류");
        if (strstr(out, "SIG") != NULL) {
            fprintf(stderr, "FAIL: exit code 3 was described as a signal\n");
            ++failures;
        } else {
            printf("PASS: exit code 3 is not a signal\n");
        }
    }

    in.exit_code = 139;
    failures += check_crash("exit code 139 remains ambiguous", &in, "종료 코드 오류");
    if (analyze_crash(&in, out, sizeof(out)) != 0 || strstr(out, "SIGSEGV") != NULL) {
        fprintf(stderr, "FAIL: exit code 139 was treated as SIGSEGV\n");
        ++failures;
    } else {
        printf("PASS: exit code 139 is not assumed to be SIGSEGV\n");
    }

    in.exit_code = 0;
    in.elapsed_ms = 50;
    in.maxrss_kb = 600000;
    strcpy(in.last_output, "reading line 17");
    if (analyze_crash(&in, out, sizeof(out)) != 0) {
        fprintf(stderr, "FAIL: execution metadata analysis\n");
        ++failures;
    } else {
        failures += check_contains("short runtime hint", out, "시작 직후");
        failures += check_contains("high memory hint", out, "과도한 메모리 사용");
        failures += check_contains("last output", out, "reading line 17");
    }
    in.last_output[0] = '\0';
    failures += check_crash("empty last output", &in, "출력 정보 없음");

    for (i = 0; i < sizeof(build_cases) / sizeof(build_cases[0]); ++i) {
        failures += check_build(build_cases[i].name, build_cases[i].message,
                                build_cases[i].expected);
    }
    failures += check_build("GCC source location", "main.c:20:4: error: 'x' undeclared",
                            "main.c:20");

    if (failures != 0) {
        fprintf(stderr, "%d analyzer checks failed\n", failures);
        return 1;
    }
    printf("All analyzer checks passed\n");
    return 0;
}
