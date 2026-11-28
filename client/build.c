#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "build.h"
#include "protocol.h"

static int is_source_marker(const char *line)
{
    line += strspn(line, " \t");
    line += strspn(line, "0123456789");
    line += strspn(line, " \t");
    return *line == '|';
}

static size_t collect_diagnostics(FILE *stream, char *body)
{
    char *line = NULL;
    size_t capacity = 0;
    size_t used = 0;
    int collected = 0;
    ssize_t length;

    while ((length = getline(&line, &capacity, stream)) >= 0) {
        fwrite(line, 1, (size_t)length, stderr);

        /* 첫 오류와 바로 이어지는 소스·위치 표시 줄만 전송한다. */
        if (collected == 2)
            continue;
        int source_marker = is_source_marker(line);
        if (collected == 0) {
            /* 링커의 원인 줄은 collect2 종료 메시지보다 먼저 나온다. */
            if (source_marker ||
                (!strstr(line, ": error:") && !strstr(line, ": fatal error:") &&
                 !strstr(line, ": undefined reference to ")))
                continue;
            collected = 1;
        } else if (!source_marker) {
            collected = 2;
            continue;
        }

        size_t count = (size_t)length;
        if (count > MSG_MAX_BODY - used)
            count = MSG_MAX_BODY - used;
        memcpy(body + used, line, count);
        used += count;
    }
    free(line);
    return used;
}

static int is_source_file(const char *path)
{
    size_t len = strlen(path);
    const char *base = strrchr(path, '/');
    base = base == NULL ? path : base + 1;
    if (strcmp(base, "test_analyzer.c") == 0)
        return 0;
    return (len >= 2 && strcmp(path + len - 2, ".c") == 0) ||
           (len >= 2 && strcmp(path + len - 2, ".h") == 0);
}

static size_t collect_sources(char *const argv[], char *out, size_t outsz)
{
    size_t used = 0;
    for (size_t i = 1; argv[i] != NULL && used + 1 < outsz; ++i) {
        FILE *fp;
        int n;
        size_t got;
        if (!is_source_file(argv[i]))
            continue;
        fp = fopen(argv[i], "rb");
        if (fp == NULL)
            continue;
        n = snprintf(out + used, outsz - used, "\n--- %s ---\n", argv[i]);
        if (n < 0 || (size_t)n >= outsz - used) {
            fclose(fp);
            break;
        }
        used += (size_t)n;
        got = fread(out + used, 1, outsz - used - 1, fp);
        used += got;
        fclose(fp);
        out[used] = '\0';
        if (used + 1 >= outsz)
            break;
    }
    return used;
}

int build_program(char *const argv[], const struct server_address *server)
{
    int diagnostics[2];
    if (pipe(diagnostics) < 0) {
        perror("pipe");
        return 1;
    }

    /* GCC 진단은 stderr로 받고, stdout은 부모와 같은 곳에 출력한다. */
    pid_t pid = fork();
    if (pid == 0) {
        close(diagnostics[0]);
        if (dup2(diagnostics[1], STDERR_FILENO) < 0) {
            perror("dup2");
            _exit(126);
        }
        if (diagnostics[1] != STDERR_FILENO)
            close(diagnostics[1]);
        /* GCC 진단의 error: 표기를 일정하게 유지한다. */
        if (setenv("LC_ALL", "C", 1) < 0) {
            perror("setenv");
            _exit(126);
        }
        execvp(argv[0], argv);
        perror(argv[0]);
        _exit(127);
    }
    close(diagnostics[1]);
    if (pid < 0) {
        perror("fork");
        close(diagnostics[0]);
        return 1;
    }

    char body[MSG_MAX_BODY];
    char sources[8192] = {0};
    size_t length = 0;
    size_t source_length;
    int read_failed = 0;
    FILE *stream = fdopen(diagnostics[0], "r");
    if (stream) {
        length = collect_diagnostics(stream, body);
        if (!feof(stream)) {
            perror("gcc stderr");
            read_failed = 1;
        }
        fclose(stream);
    } else {
        perror("fdopen");
        close(diagnostics[0]);
        read_failed = 1;
    }

    int status;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        perror("waitpid");
        return 1;
    }
    int result = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    source_length = collect_sources(argv, sources, sizeof(sources));
    if (result != 0 && length > 0 && !read_failed) {
        size_t marker_len = strlen(MSG_BUILD_SOURCE_MARKER);
        if (source_length > 0 && marker_len + source_length <= sizeof(body) - length) {
            memcpy(body + length, MSG_BUILD_SOURCE_MARKER, marker_len);
            length += marker_len;
            memcpy(body + length, sources, source_length);
            length += source_length;
        }
        request_server(server, MSG_BUILD, body, (uint32_t)length);
    }
    return result == 0 && read_failed ? 1 : result;
}
