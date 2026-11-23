#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "protocol.h"
#include "run.h"

#define LAST_LINES 10

/* 마지막 출력 필드 크기만큼 최근 바이트를 순환 저장한다. */
struct output_tail {
    char bytes[sizeof(((crash_info *)0)->last_output) - 1];
    size_t next;
    size_t used;
};

/* 핸들러는 번호만 남기고 터미널 복구는 부모가 한다. */
static volatile sig_atomic_t interrupted;

static void note_signal(int signo)
{
    interrupted = signo;
}

static void remember_output(struct output_tail *tail, const char *buf, size_t len)
{
    /* 화면에는 원본을 보내고 기록에서는 PTY의 CR을 뺀다. */
    for (size_t i = 0; i < len; ++i) {
        if (buf[i] == '\r')
            continue;
        tail->bytes[tail->next] = buf[i] == '\0' ? '?' : buf[i];
        tail->next = (tail->next + 1) % sizeof(tail->bytes);
        if (tail->used < sizeof(tail->bytes))
            ++tail->used;
    }
}

static void copy_last_lines(const struct output_tail *tail, char *out)
{
    size_t first = (tail->next + sizeof(tail->bytes) - tail->used)
                   % sizeof(tail->bytes);
    size_t offset = tail->used;
    unsigned lines = 0;

    /* 끝에서 줄을 세되 마지막 줄바꿈 뒤의 빈 줄은 세지 않는다. */
    while (offset > 0) {
        --offset;
        if (tail->bytes[(first + offset) % sizeof(tail->bytes)] == '\n'
            && offset + 1 < tail->used && ++lines == LAST_LINES) {
            ++offset;
            break;
        }
    }

    size_t len = tail->used - offset;
    for (size_t i = 0; i < len; ++i)
        out[i] = tail->bytes[(first + offset + i) % sizeof(tail->bytes)];
    out[len] = '\0';
}

static int write_bytes(int fd, const char *buf, size_t len)
{
    while (len > 0 && !interrupted) {
        ssize_t count = write(fd, buf, len);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return -1;
        buf += count;
        len -= (size_t)count;
    }
    return len == 0 ? 0 : -1;
}

static int relay(int master, pid_t pid, struct output_tail *tail,
                 int *status, struct rusage *usage, int *reaped,
                 struct timespec *end)
{
    struct pollfd fds[] = {
        { .fd = master, .events = POLLIN },
        { .fd = STDIN_FILENO, .events = POLLIN }
    };
    char buf[4096];

    /* 자식 종료를 확인하면서 PTY 출력과 표준 입력을 중계한다. */
    while (!interrupted) {
        if (!*reaped) {
            pid_t result = wait4(pid, status, WNOHANG, usage);
            if (result < 0) {
                if (errno == EINTR)
                    continue;
                return -1;
            }
            if (result == pid) {
                *reaped = 1;
                fds[1].fd = -1;
                if (clock_gettime(CLOCK_MONOTONIC, end) < 0)
                    return -1;
            }
        }

        /* 종료를 확인한 뒤에는 PTY에 남은 출력만 읽는다. */
        int ready = poll(fds, 2, *reaped ? 0 : 50);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (ready == 0 && *reaped)
            return 0;

        if (fds[0].revents) {
            ssize_t count = read(master, buf, sizeof(buf));
            if (count > 0) {
                remember_output(tail, buf, (size_t)count);
                if (write_bytes(STDOUT_FILENO, buf, (size_t)count) < 0)
                    return -1;
            } else if (count == 0 || errno == EIO) {
                /* Linux PTY는 자식 쪽이 닫히면 EIO를 반환할 수 있다. */
                fds[0].fd = -1;
                fds[1].fd = -1;
            } else if (errno != EINTR) {
                return -1;
            }
        }
        if (!*reaped && fds[0].fd >= 0 && fds[1].revents) {
            ssize_t count = read(STDIN_FILENO, buf, 256);
            if (count > 0) {
                if (write_bytes(master, buf, (size_t)count) < 0 && errno != EIO)
                    return -1;
            } else if (count == 0) {
                fds[1].fd = -1;
            } else if (errno != EINTR) {
                return -1;
            }
        }
    }
    return -1;
}

/* 줄 단위 프로토콜에서 cmd가 한 줄로 유지되게 한다. */
static void format_command(char *out, size_t size, char *const argv[])
{
    size_t used = 0;
    for (size_t i = 0; argv[i] && used < size - 1; ++i) {
        if (i != 0)
            out[used++] = ' ';
        for (const char *p = argv[i]; *p && used < size - 1; ++p)
            out[used++] = *p == '\n' || *p == '\r' ? ' ' : *p;
    }
    out[used] = '\0';
}

static void print_crash_summary(const crash_info *info)
{
    printf("\n종료 원인: ");
    if (info->signo != 0)
        printf("%s (시그널 %d)\n", strsignal(info->signo), info->signo);
    else
        printf("종료 코드 %d\n", info->exit_code);

    printf("실행 시간: %ld ms\n", info->elapsed_ms);
    printf("최대 메모리 사용량: %ld KB\n", info->maxrss_kb);
    fflush(stdout);
}

int run_program(char *const argv[], const struct server_address *server)
{
    const int signals[] = { SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGPIPE };
    struct sigaction action = { .sa_handler = note_signal }, previous[sizeof(signals) / sizeof(signals[0])];
    struct termios original, raw;
    struct winsize size;
    struct timespec start, end;
    struct rusage usage;
    struct output_tail tail = {0};
    crash_info info = {0};
    int terminal = isatty(STDIN_FILENO);
    int raw_enabled = 0;
    int master = -1;
    int exec_pipe[2] = { -1, -1 };
    int exec_error = 0;
    int status, saved_errno, reaped = 0, result = 1;
    size_t installed = 0;
    pid_t pid = -1;
    const char *error = NULL;

    interrupted = 0;
    if (terminal && tcgetattr(STDIN_FILENO, &original) < 0) {
        perror("tcgetattr");
        return 1;
    }

    sigemptyset(&action.sa_mask);
    for (; installed < sizeof(signals) / sizeof(signals[0]); ++installed) {
        if (sigaction(signals[installed], &action, &previous[installed]) < 0) {
            error = "sigaction";
            goto cleanup;
        }
    }

    /* exec 성공 시 쓰기 끝이 닫히고, 실패 시 자식이 errno를 보낸다. */
    if (pipe2(exec_pipe, O_CLOEXEC) < 0) {
        error = "pipe2";
        goto cleanup;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &start) < 0) {
        error = "clock_gettime";
        goto cleanup;
    }
    /* 자식에 터미널을 붙여 크래시 직전의 줄 출력도 받을 수 있게 한다. */
    pid = forkpty(&master, NULL, terminal ? &original : NULL,
                  terminal && ioctl(STDIN_FILENO, TIOCGWINSZ, &size) == 0
                  ? &size : NULL);
    if (pid < 0) {
        error = "forkpty";
        goto cleanup;
    }
    if (pid == 0) {
        close(exec_pipe[0]);
        for (size_t i = 0; i < installed; ++i)
            sigaction(signals[i], &previous[i], NULL);
        execvp(argv[0], argv);
        int failure = errno;
        while (write(exec_pipe[1], &failure, sizeof(failure)) < 0 && errno == EINTR)
            continue;
        _exit(127);
    }

    close(exec_pipe[1]);
    exec_pipe[1] = -1;

    /* 부모 터미널을 raw 모드로 바꿔 키 입력을 그대로 넘긴다. */
    if (terminal) {
        raw = original;
        cfmakeraw(&raw);
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0) {
            error = "tcsetattr";
            goto cleanup;
        }
        raw_enabled = 1;
    }
    if (relay(master, pid, &tail, &status, &usage, &reaped, &end) < 0) {
        if (!interrupted)
            error = "crashdoc run";
        goto cleanup;
    }

    result = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    /* 자식을 회수했으므로 파이프에는 errno 또는 EOF가 남아 있다. */
    ssize_t received;
    do {
        received = read(exec_pipe[0], &exec_error, sizeof(exec_error));
    } while (received < 0 && errno == EINTR && !interrupted);
    if (received < 0 || (received != 0 && received != sizeof(exec_error))) {
        if (received > 0)
            errno = EIO;
        error = "execvp 상태 확인";
        result = 1;
        goto cleanup;
    }
    if (received != 0)
        goto cleanup;

    info.signo = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    info.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    info.elapsed_ms = ((end.tv_sec - start.tv_sec) * 1000000000LL
                       + end.tv_nsec - start.tv_nsec) / 1000000LL;
    info.maxrss_kb = usage.ru_maxrss;
    format_command(info.cmd, sizeof(info.cmd), argv);
    copy_last_lines(&tail, info.last_output);

cleanup:
    /* 정상 종료, 오류, 시그널 모두 이곳에서 터미널을 복구한다. */
    saved_errno = errno;
    if (raw_enabled) {
        int restored;
        do {
            restored = tcsetattr(STDIN_FILENO, TCSANOW, &original);
        } while (restored < 0 && errno == EINTR);
        if (restored < 0) {
            perror("터미널 복구");
            result = 1;
        }
    }
    if (error && !interrupted) {
        errno = saved_errno;
        perror(error);
    }
    /* 중계가 중단됐으면 남은 자식 프로세스를 정리한다. */
    if (pid > 0 && !reaped) {
        if (kill(-pid, SIGKILL) < 0)
            kill(pid, SIGKILL);
        while (wait4(pid, &status, 0, &usage) < 0 && errno == EINTR)
            continue;
    }
    if (master >= 0)
        close(master);
    for (size_t i = 0; i < 2; ++i) {
        if (exec_pipe[i] >= 0)
            close(exec_pipe[i]);
    }

    while (installed > 0) {
        --installed;
        sigaction(signals[installed], &previous[installed], NULL);
    }
    if (interrupted)
        return 128 + interrupted;
    if (exec_error != 0 && !error) {
        fprintf(stderr, "실행 실패: %s: %s\n", argv[0], strerror(exec_error));
        return result;
    }

    /* 마지막 출력은 서버에 보내는 본문에만 넣는다. */
    if (reaped && (info.signo != 0 || info.exit_code != 0)) {
        print_crash_summary(&info);
        char body[sizeof(info) + 128];
        int length = crash_info_encode(&info, body, sizeof(body));
        if (length < 0) {
            fprintf(stderr, "크래시 정보를 인코딩할 수 없습니다.\n");
        } else {
            request_analysis(server, body, (uint32_t)length);
        }
    }

    return result;
}
