#include "analyzer_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int append_build_location(const char *text, char *location, size_t size)
{
    const char *p;

    if (text == NULL || location == NULL || size == 0)
        return -1;

    for (p = text; *p != '\0'; ++p) {
        const char *q;
        const char *digits;
        size_t file_len;
        size_t line_len;

        if (*p != ':')
            continue;
        digits = p + 1;
        if (!isdigit((unsigned char)*digits))
            continue;
        q = digits;
        while (isdigit((unsigned char)*q))
            ++q;
        if (*q != ':')
            continue;
        file_len = (size_t)(p - text);
        line_len = (size_t)(q - digits);
        if (file_len == 0 || file_len >= size || line_len >= size - file_len - 2)
            return -1;
        if (snprintf(location, size, "%.*s:%.*s", (int)file_len, text,
                     (int)line_len, digits) >= (int)size)
            return -1;
        return 0;
    }
    location[0] = '\0';
    return 0;
}
int rule_analyze_crash(const crash_info *in, char *out, size_t outsz)
{
    const char *type = "비정상 종료";
    const char *cause = "프로그램이 정상 종료되지 않았습니다.";
    const char *check = "종료 코드와 프로그램의 마지막 동작을 확인하세요.";
    const char *signal_name = NULL;
    int n;

    if (in == NULL || out == NULL || outsz == 0)
        return -1;

    if (in->signo > 0) {
        type = "시그널 종료";
        switch (in->signo) {
        case 11:
            signal_name = "SIGSEGV";
            cause = "잘못된 메모리 접근으로 종료되었습니다.";
            check = "포인터 유효성, 배열 접근 범위, 메모리 해제 여부를 확인하세요.";
            break;
        case 8:
            signal_name = "SIGFPE";
            cause = "산술 연산 중 예외가 발생해 종료되었습니다.";
            check = "0으로 나누기와 정수 연산의 경계값을 확인하세요.";
            break;
        case 6:
            signal_name = "SIGABRT";
            cause = "프로그램 또는 런타임이 중단을 요청했습니다.";
            check = "assert 실패, 런타임 오류, 메모리 관리 오류가 있었는지 확인하세요.";
            break;
        case 7:
            signal_name = "SIGBUS";
            cause = "유효하지 않거나 정렬되지 않은 메모리 접근 가능성이 있습니다.";
            check = "메모리 매핑과 포인터 정렬, 접근 가능한 메모리 범위를 확인하세요.";
            break;
        case 4:
            signal_name = "SIGILL";
            cause = "실행할 수 없는 명령어가 실행된 것으로 확인됩니다.";
            check = "잘못된 함수 포인터, 손상된 실행 흐름, 대상 CPU와 빌드 옵션을 확인하세요.";
            break;
        case 15:
            signal_name = "SIGTERM";
            cause = "외부에서 종료 요청을 받아 종료되었습니다.";
            check = "프로그램이나 실행 환경이 종료 요청을 보냈는지 확인하세요.";
            break;
        case 2:
            signal_name = "SIGINT";
            type = "사용자 종료";
            cause = "SIGINT 인터럽트(보통 Ctrl+C)를 받아 프로그램이 중단되었습니다.";
            check = "의도한 중단이면 정상적인 사용자 종료입니다. 의도하지 않았다면 터미널이나 실행 환경에서 SIGINT를 보낸 경로를 확인하세요.";
            break;
        case 9:
            signal_name = "SIGKILL";
            cause = "강제 종료 요청을 받아 프로세스가 종료되었습니다.";
            check = "시간 제한이나 메모리 제한 등 실행 환경의 종료 조건을 확인하세요.";
            break;
        default:
            signal_name = "알 수 없는 시그널";
            cause = "시그널에 의해 프로그램이 종료되었습니다.";
            check = "실행 환경의 시그널 발생 원인과 프로그램 상태를 확인하세요.";
            break;
        }
    } else if (in->timed_out) {
        type = "시간 초과";
        cause = "제한 시간 안에 실행이 끝나지 않았습니다. 무한 루프 또는 장시간 작업일 수 있습니다.";
        check = "반복문의 종료 조건과 입력 대기, 처리량이 큰 작업을 확인하세요.";
    } else if (in->exit_code != 0) {
        type = "종료 코드 오류";
        cause = "프로그램이 시그널이 아닌 종료 코드로 비정상 종료를 보고했습니다.";
        check = "종료 코드가 설정된 경로와 오류가 발생한 작업을 확인하세요.";
    } else {
        type = "정상 종료";
        cause = "프로그램이 종료 코드 0으로 종료되었습니다.";
        check = "추가로 확인할 비정상 종료 정보가 없습니다.";
    }

    n = snprintf(out, outsz,
                 "[오류 분석 결과]\n오류 유형: %s\n원인 추정:\n%s\n",
                 type, cause);
    if (n < 0 || (size_t)n >= outsz)
        return -1;
    if (signal_name != NULL) {
        int used = n;
        n = snprintf(out + used, outsz - (size_t)used,
                     "시그널: %s(%d)\n", signal_name, in->signo);
        if (n < 0 || (size_t)n >= outsz - (size_t)used)
            return -1;
    } else if (in->signo > 0) {
        int used = n;
        n = snprintf(out + used, outsz - (size_t)used,
                     "시그널 번호: %d\n", in->signo);
        if (n < 0 || (size_t)n >= outsz - (size_t)used)
            return -1;
    }

    {
        size_t used = strlen(out);
        n = snprintf(out + used, outsz - used,
                     "점검:\n%s\n실행 시간: %ld ms\n최대 메모리: %ld KB\n",
                     check, in->elapsed_ms, in->maxrss_kb);
        if (n < 0 || (size_t)n >= outsz - used)
            return -1;
    }
    if (in->elapsed_ms >= 0 && in->elapsed_ms <= 100) {
        size_t used = strlen(out);
        n = snprintf(out + used, outsz - used,
                     "실행 시간이 매우 짧아 시작 직후 종료되었을 가능성이 있습니다.\n");
        if (n < 0 || (size_t)n >= outsz - used)
            return -1;
    }
    if (in->maxrss_kb > 512L * 1024L) {
        size_t used = strlen(out);
        n = snprintf(out + used, outsz - used,
                     "최대 메모리 사용량이 커서 과도한 메모리 사용 가능성이 있습니다.\n");
        if (n < 0 || (size_t)n >= outsz - used)
            return -1;
    }
    if (in->timed_out && in->signo > 0) {
        size_t used = strlen(out);
        n = snprintf(out + used, outsz - used,
                     "실행 시간 제한 초과 정보도 전달되었습니다.\n");
        if (n < 0 || (size_t)n >= outsz - used)
            return -1;
    }
    {
        size_t used = strlen(out);
        const char *last = in->last_output[0] == '\0' ? "(출력 정보 없음)" : in->last_output;
        n = snprintf(out + used, outsz - used,
                     "마지막 출력:\n%s\n", last);
        if (n < 0 || (size_t)n >= outsz - used)
            return -1;
        if (in->last_output[0] != '\0') {
            used = strlen(out);
            n = snprintf(out + used, outsz - used,
                         "해당 내용이 마지막으로 출력된 뒤 프로그램이 종료된 것으로 확인됩니다.\n");
            if (n < 0 || (size_t)n >= outsz - used)
                return -1;
        }
    }
    return 0;
}

int rule_analyze_build(const char *error_text, char *out, size_t outsz)
{
    const char *cause = "컴파일 오류 메시지를 규칙으로 분류하지 못했습니다.";
    const char *check = "오류 메시지의 첫 줄과 표시된 코드 위치를 확인하세요.";
    const char *category = "컴파일 오류";
    char location[512];
    int n;

    if (error_text == NULL || out == NULL || outsz == 0)
        return -1;

    if (strstr(error_text, "undeclared") != NULL) {
        cause = "선언되지 않은 변수 또는 함수를 사용했습니다.";
        check = "사용 전에 이름을 선언했는지, 철자가 맞는지 확인하세요.";
    } else if (strstr(error_text, "expected ';'") != NULL ||
               strstr(error_text, "expected ‘;’") != NULL ||
               strstr(error_text, "expected ‘;'") != NULL ||
               strstr(error_text, "expected ';'") != NULL) {
        cause = "문장에 필요한 세미콜론이 누락되었을 가능성이 있습니다.";
        check = "표시된 위치와 바로 앞 문장의 끝에 세미콜론이 있는지 확인하세요.";
    } else if (strstr(error_text, "implicit declaration of function") != NULL) {
        cause = "함수 선언을 확인하지 못한 상태에서 함수를 사용했습니다.";
        check = "필요한 헤더를 포함했는지, 함수 이름과 선언을 확인하세요.";
    } else if (strstr(error_text, "incompatible pointer type") != NULL ||
               strstr(error_text, "incompatible pointer types") != NULL) {
        cause = "대입 또는 함수 호출에서 포인터 자료형이 서로 맞지 않습니다.";
        check = "포인터가 가리키는 자료형과 함수 매개변수 자료형을 맞추세요.";
    } else if (strstr(error_text, "undefined reference") != NULL) {
        cause = "링커가 참조된 함수나 변수의 정의를 찾지 못했습니다.";
        check = "정의가 빌드에 포함되었는지와 필요한 라이브러리 연결 옵션을 확인하세요.";
        category = "링크 오류";
    }

    if (append_build_location(error_text, location, sizeof(location)) != 0)
        return -1;
    n = snprintf(out, outsz, "[컴파일 오류 분석]\n원인 추정:\n%s\n", cause);
    if (n < 0 || (size_t)n >= outsz)
        return -1;
    if (location[0] != '\0') {
        size_t used = (size_t)n;
        n = snprintf(out + used, outsz - used, "발생 위치:\n%s\n", location);
        if (n < 0 || (size_t)n >= outsz - used)
            return -1;
    }
    {
        size_t used = strlen(out);
        const char *line_end = strchr(error_text, '\n');
        size_t excerpt_len = line_end == NULL ? strlen(error_text) : (size_t)(line_end - error_text);
        if (excerpt_len > 400)
            excerpt_len = 400;
        n = snprintf(out + used, outsz - used,
                     "오류 메시지: %.*s\n점검:\n%s\n오류 분류: %s\n",
                     (int)excerpt_len, error_text, check, category);
        if (n < 0 || (size_t)n >= outsz - used)
            return -1;
    }
    return 0;
}
