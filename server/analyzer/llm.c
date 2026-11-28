#define _POSIX_C_SOURCE 200809L

#include "analyzer_internal.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PROMPT_CAP 14000
#define REQUEST_CAP 100000
#define RESPONSE_CAP 65536

static int json_escape(const char *src, char *dst, size_t dstsz)
{
    size_t used = 0;
    const unsigned char *p = (const unsigned char *)src;

    if (src == NULL || dst == NULL || dstsz == 0)
        return -1;
    while (*p != '\0') {
        char escaped[7];
        size_t count;
        if (*p == '"' || *p == '\\') {
            escaped[0] = '\\';
            escaped[1] = (char)*p;
            count = 2;
        } else if (*p == '\b' || *p == '\f' || *p == '\n' ||
                   *p == '\r' || *p == '\t') {
            escaped[0] = '\\';
            escaped[1] = *p == '\b' ? 'b' : *p == '\f' ? 'f' :
                         *p == '\n' ? 'n' : *p == '\r' ? 'r' : 't';
            count = 2;
        } else if (*p < 0x20) {
            int n = snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
            if (n != 6)
                return -1;
            count = 6;
        } else {
            escaped[0] = (char)*p;
            count = 1;
        }
        if (count >= dstsz - used)
            return -1;
        memcpy(dst + used, escaped, count);
        used += count;
        ++p;
    }
    dst[used] = '\0';
    return 0;
}

static int append_codepoint(char *out, size_t outsz, size_t *used, unsigned int cp)
{
    unsigned char bytes[4];
    size_t count;

    if (cp == 0)
        return -1;
    if (cp <= 0x7f) {
        bytes[0] = (unsigned char)cp;
        count = 1;
    } else if (cp <= 0x7ff) {
        bytes[0] = (unsigned char)(0xc0 | (cp >> 6));
        bytes[1] = (unsigned char)(0x80 | (cp & 0x3f));
        count = 2;
    } else if (cp <= 0xffff) {
        bytes[0] = (unsigned char)(0xe0 | (cp >> 12));
        bytes[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
        bytes[2] = (unsigned char)(0x80 | (cp & 0x3f));
        count = 3;
    } else if (cp <= 0x10ffff) {
        bytes[0] = (unsigned char)(0xf0 | (cp >> 18));
        bytes[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3f));
        bytes[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
        bytes[3] = (unsigned char)(0x80 | (cp & 0x3f));
        count = 4;
    } else {
        return -1;
    }
    if (count >= outsz - *used)
        return -1;
    memcpy(out + *used, bytes, count);
    *used += count;
    return 0;
}

static int read_hex4(const char *p, unsigned int *value)
{
    unsigned int result = 0;
    int i;

    for (i = 0; i < 4; ++i) {
        unsigned char c = (unsigned char)p[i];
        unsigned int digit;
        if (c >= '0' && c <= '9')
            digit = (unsigned int)(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = (unsigned int)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            digit = (unsigned int)(c - 'A' + 10);
        else
            return -1;
        result = (result << 4) | digit;
    }
    *value = result;
    return 0;
}

static void skip_json_space(const char **p)
{
    while (isspace((unsigned char)**p))
        ++*p;
}

static int parse_json_string(const char **p)
{
    if (**p != '"')
        return -1;
    ++*p;
    while (**p != '\0' && **p != '"') {
        unsigned int cp;
        if ((unsigned char)**p < 0x20)
            return -1;
        if (**p != '\\') {
            ++*p;
            continue;
        }
        ++*p;
        if (**p == '\0')
            return -1;
        if (strchr("\"\\/bfnrt", **p) != NULL) {
            ++*p;
            continue;
        }
        if (**p != 'u' || strlen(*p + 1) < 4 || read_hex4(*p + 1, &cp) != 0)
            return -1;
        *p += 5;
        if (cp >= 0xd800 && cp <= 0xdbff) {
            unsigned int low;
            if (strlen(*p) < 6 || (*p)[0] != '\\' || (*p)[1] != 'u' ||
                strlen(*p + 2) < 4 ||
                read_hex4(*p + 2, &low) != 0 || low < 0xdc00 || low > 0xdfff)
                return -1;
            *p += 6;
        } else if (cp >= 0xdc00 && cp <= 0xdfff) {
            return -1;
        }
    }
    if (**p != '"')
        return -1;
    ++*p;
    return 0;
}

static int parse_json_number(const char **p)
{
    const char *start = *p;

    if (**p == '-')
        ++*p;
    if (**p == '0') {
        ++*p;
        if (isdigit((unsigned char)**p))
            return -1;
    } else {
        if (**p < '1' || **p > '9')
            return -1;
        while (isdigit((unsigned char)**p))
            ++*p;
    }
    if (**p == '.') {
        ++*p;
        if (!isdigit((unsigned char)**p))
            return -1;
        while (isdigit((unsigned char)**p))
            ++*p;
    }
    if (**p == 'e' || **p == 'E') {
        ++*p;
        if (**p == '+' || **p == '-')
            ++*p;
        if (!isdigit((unsigned char)**p))
            return -1;
        while (isdigit((unsigned char)**p))
            ++*p;
    }
    return *p == start ? -1 : 0;
}

static int parse_json_value(const char **p, unsigned int depth)
{
    if (depth > 32)
        return -1;
    skip_json_space(p);
    if (**p == '"')
        return parse_json_string(p);
    if (**p == '{') {
        ++*p;
        skip_json_space(p);
        if (**p == '}') {
            ++*p;
            return 0;
        }
        for (;;) {
            if (parse_json_string(p) != 0)
                return -1;
            skip_json_space(p);
            if (**p != ':')
                return -1;
            ++*p;
            if (parse_json_value(p, depth + 1) != 0)
                return -1;
            skip_json_space(p);
            if (**p == '}') {
                ++*p;
                return 0;
            }
            if (**p != ',')
                return -1;
            ++*p;
            skip_json_space(p);
        }
    }
    if (**p == '[') {
        ++*p;
        skip_json_space(p);
        if (**p == ']') {
            ++*p;
            return 0;
        }
        for (;;) {
            if (parse_json_value(p, depth + 1) != 0)
                return -1;
            skip_json_space(p);
            if (**p == ']') {
                ++*p;
                return 0;
            }
            if (**p != ',')
                return -1;
            ++*p;
            skip_json_space(p);
        }
    }
    if (strncmp(*p, "true", 4) == 0) {
        *p += 4;
        return 0;
    }
    if (strncmp(*p, "false", 5) == 0) {
        *p += 5;
        return 0;
    }
    if (strncmp(*p, "null", 4) == 0) {
        *p += 4;
        return 0;
    }
    return parse_json_number(p);
}

static int validate_json_object(const char *json)
{
    const char *p = json;

    skip_json_space(&p);
    if (*p != '{' || parse_json_value(&p, 0) != 0)
        return -1;
    skip_json_space(&p);
    return *p == '\0' ? 0 : -1;
}

static int extract_response(const char *json, char *out, size_t outsz)
{
    const char *key = json;
    const char *p = NULL;
    size_t used = 0;

    if (json == NULL || out == NULL || outsz == 0)
        return -1;
    if (validate_json_object(json) != 0)
        return -1;
    while ((key = strstr(key, "\"response\"")) != NULL) {
        const char *q = key + sizeof("\"response\"") - 1;
        while (isspace((unsigned char)*q))
            ++q;
        if (*q++ != ':') {
            key += sizeof("\"response\"") - 1;
            continue;
        }
        while (isspace((unsigned char)*q))
            ++q;
        if (*q == '"') {
            p = q + 1;
            break;
        }
        key += sizeof("\"response\"") - 1;
    }
    if (p == NULL)
        return -1;

    while (*p != '\0' && *p != '"') {
        unsigned int cp;
        if ((unsigned char)*p < 0x20)
            return -1;
        if (*p != '\\') {
            if (used + 1 >= outsz)
                return -1;
            out[used++] = *p++;
            continue;
        }
        ++p;
        if (*p == '\0')
            return -1;
        switch (*p++) {
        case '"': cp = '"'; break;
        case '\\': cp = '\\'; break;
        case '/': cp = '/'; break;
        case 'b': cp = '\b'; break;
        case 'f': cp = '\f'; break;
        case 'n': cp = '\n'; break;
        case 'r': cp = '\r'; break;
        case 't': cp = '\t'; break;
        case 'u':
            if (strlen(p) < 4 || read_hex4(p, &cp) != 0)
                return -1;
            p += 4;
            if (cp >= 0xd800 && cp <= 0xdbff) {
                unsigned int low;
                if (p[0] != '\\' || p[1] != 'u' || strlen(p + 2) < 4 ||
                    read_hex4(p + 2, &low) != 0 || low < 0xdc00 || low > 0xdfff)
                    return -1;
                cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                p += 6;
            } else if (cp >= 0xdc00 && cp <= 0xdfff) {
                return -1;
            }
            break;
        default:
            return -1;
        }
        if (append_codepoint(out, outsz, &used, cp) != 0)
            return -1;
    }
    if (*p != '"')
        return -1;
    out[used] = '\0';
    while (used > 0 && isspace((unsigned char)out[used - 1]))
        out[--used] = '\0';
    p = out;
    while (isspace((unsigned char)*p))
        ++p;
    if (*p == '\0')
        return -1;
    if (p != out)
        memmove(out, p, strlen(p) + 1);
    return 0;
}

static int run_curl(const char *request, char *response, size_t responsesz)
{
    int pipefd[2];
    pid_t pid;
    int status = 0;
    int child_done = 0;
    int eof = 0;
    int failed = 0;
    size_t used = 0;
    struct timespec start;
    char *const argv[] = {
        (char *)"curl", (char *)"--silent", (char *)"--fail",
        (char *)"--max-time", (char *)"15", (char *)"-H",
        (char *)"Content-Type: application/json", (char *)"--data-binary",
        (char *)request, (char *)"http://localhost:11434/api/generate", NULL
    };

    if (pipe(pipefd) != 0)
        return -1;
    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0)
            _exit(127);
        close(pipefd[1]);
        execvp("curl", argv);
        _exit(127);
    }
    close(pipefd[1]);
    if (fcntl(pipefd[0], F_SETFL, fcntl(pipefd[0], F_GETFL) | O_NONBLOCK) < 0) {
        close(pipefd[0]);
        kill(pid, SIGKILL);
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
            ;
        return -1;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        close(pipefd[0]);
        kill(pid, SIGKILL);
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
            ;
        return -1;
    }

    while (!eof || !child_done) {
        struct timespec now;
        long long remaining;
        int wait_ms;
        struct pollfd pfd;
        pid_t waited;

        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            failed = 1;
            break;
        }
        remaining = 15000LL - ((long long)(now.tv_sec - start.tv_sec) * 1000LL +
                    (long long)(now.tv_nsec - start.tv_nsec) / 1000000LL);
        if (remaining <= 0) {
            failed = 1;
            break;
        }
        wait_ms = remaining > 100 ? 100 : (int)remaining;
        pfd.fd = pipefd[0];
        pfd.events = POLLIN | POLLHUP;
        pfd.revents = 0;
        if (!eof) {
            int pr = poll(&pfd, 1, wait_ms);
            if (pr < 0 && errno != EINTR) {
                failed = 1;
                break;
            }
            if (pr > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
                for (;;) {
                    ssize_t got;
                    if (used + 1 >= responsesz) {
                        failed = 1;
                        break;
                    }
                    got = read(pipefd[0], response + used,
                               responsesz - used - 1);
                    if (got > 0) {
                        used += (size_t)got;
                        continue;
                    }
                    if (got == 0) {
                        eof = 1;
                        break;
                    }
                    if (errno == EINTR)
                        continue;
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        failed = 1;
                        break;
                    }
                    break;
                }
            }
        } else {
            (void)poll(NULL, 0, wait_ms);
        }
        if (failed)
            break;
        response[used] = '\0';
        waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid)
            child_done = 1;
        else if (waited < 0 && errno != EINTR) {
            failed = 1;
            break;
        }
    }
    close(pipefd[0]);
    if (failed) {
        kill(pid, SIGKILL);
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
            ;
        return -1;
    }
    if (!child_done) {
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
            ;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || used == 0)
        return -1;
    response[used] = '\0';
    return 0;
}

static int ollama_request(const char *prompt, char *out, size_t outsz)
{
    char *escaped = NULL;
    char *request = NULL;
    char *response = NULL;
    int result = -1;
    int n;

    if (prompt == NULL || out == NULL || outsz == 0)
        return -1;
    escaped = malloc(REQUEST_CAP);
    request = malloc(REQUEST_CAP);
    response = malloc(RESPONSE_CAP);
    if (escaped == NULL || request == NULL || response == NULL)
        goto done;
    if (json_escape(prompt, escaped, REQUEST_CAP) != 0)
        goto done;
    n = snprintf(request, REQUEST_CAP,
                 "{\"model\":\"qwen3.5:9b\",\"prompt\":\"%s\",\"stream\":false}",
                 escaped);
    if (n < 0 || (size_t)n >= REQUEST_CAP)
        goto done;
    if (run_curl(request, response, RESPONSE_CAP) != 0)
        goto done;
    if (extract_response(response, out, outsz) != 0)
        goto done;
    result = 0;

done:
    free(escaped);
    free(request);
    free(response);
    return result;
}

static void add_instructions(char *prompt, size_t promptsz, size_t *used)
{
    int n = snprintf(prompt + *used, promptsz - *used,
                     "\n분석 지침: 전달된 정보에 근거해서만 분석하세요. 확인되지 않은 함수명이나 코드 줄을 만들지 마세요. 소스코드가 없다면 코드 위치를 단정하지 마세요. 추정과 확인된 사실을 구분하고 초보자가 이해하기 쉽게 설명하세요. 해결을 위해 점검할 항목을 구체적으로 제시하세요. 한국어로 [오류 분석 결과] 또는 [컴파일 오류 분석], 원인 추정, 발생 위치(확인된 경우), 점검 형식으로 답하세요.");
    if (n > 0 && (size_t)n < promptsz - *used)
        *used += (size_t)n;
    else
        *used = promptsz;
}

int llm_analyze_crash(const crash_info *in, char *out, size_t outsz)
{
    char prompt[PROMPT_CAP];
    int n;
    size_t used;

    if (in == NULL || out == NULL || outsz == 0)
        return -1;
    n = snprintf(prompt, sizeof(prompt),
                 "크래시 분석을 수행하세요. 실제 수집된 실행 정보만 전달합니다.\n"
                 "실행 명령: %.255s\n시그널 번호: %d\n종료 코드: %d\n"
                 "Timeout 여부: %s\n실행 시간: %ld ms\n최대 메모리 사용량: %ld KB\n"
                 "마지막 출력:\n%.4095s\n",
                 in->cmd, in->signo, in->exit_code,
                 in->timed_out ? "예" : "아니오", in->elapsed_ms,
                 in->maxrss_kb, in->last_output);
    if (n < 0 || (size_t)n >= sizeof(prompt))
        return -1;
    used = (size_t)n;
    add_instructions(prompt, sizeof(prompt), &used);
    if (used >= sizeof(prompt))
        return -1;
    return ollama_request(prompt, out, outsz);
}

int llm_analyze_build(const char *error_text, char *out, size_t outsz)
{
    char prompt[PROMPT_CAP];
    int n;
    size_t used;

    if (error_text == NULL || out == NULL || outsz == 0)
        return -1;
    n = snprintf(prompt, sizeof(prompt),
                 "GCC 컴파일 또는 링크 오류를 분석하세요. 실제 전달된 오류 메시지:\n%.12000s\n",
                 error_text);
    if (n < 0 || (size_t)n >= sizeof(prompt))
        return -1;
    used = (size_t)n;
    add_instructions(prompt, sizeof(prompt), &used);
    if (used >= sizeof(prompt))
        return -1;
    return ollama_request(prompt, out, outsz);
}
