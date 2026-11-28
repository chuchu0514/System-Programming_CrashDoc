/* 담당: 김경민 — 크래시 기록 저장·조회 */
#define _POSIX_C_SOURCE 200809L

#include "store.h"

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define STORE_DIR "data"
#define STORE_PATH STORE_DIR "/crash_records.bin"
#define STORE_MAGIC UINT32_C(0x43524431) /* CRD1 */
#define STORE_VERSION UINT32_C(1)
#define STORE_RESULT_MAX (64U * 1024U)

/* Store is append-only. Each record has a fixed header followed by its strings. */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t id;
    int32_t signo;
    int32_t exit_code;
    int32_t timed_out;
    int64_t elapsed_ms;
    int64_t maxrss_kb;
    uint32_t cmd_len;
    uint32_t last_output_len;
    uint32_t result_len;
    uint32_t client_ip_len;
} disk_record;

typedef struct {
    disk_record record;
    off_t offset;
} record_ref;

static pthread_mutex_t store_lock = PTHREAD_MUTEX_INITIALIZER;

static int read_record(FILE *fp, disk_record *record, off_t *offset)
{
    int c;
    off_t pos = ftello(fp);
    struct stat st;

    if (pos < 0)
        return -1;
    c = fgetc(fp);
    if (c == EOF)
        return feof(fp) ? 0 : -1;
    if (fseeko(fp, pos, SEEK_SET) != 0 ||
        fread(record, sizeof(*record), 1, fp) != 1)
        return -1;
    if (record->magic != STORE_MAGIC || record->version != STORE_VERSION ||
        record->id == 0 || record->cmd_len >= sizeof(((crash_info *)0)->cmd) ||
        record->last_output_len >= sizeof(((crash_info *)0)->last_output) ||
        record->result_len > STORE_RESULT_MAX || record->client_ip_len >= 64)
        return -1;
    *offset = pos;
    if (fseeko(fp, (off_t)record->cmd_len + record->last_output_len +
               record->result_len + record->client_ip_len, SEEK_CUR) != 0)
        return -1;
    if (fstat(fileno(fp), &st) != 0 || ftello(fp) > st.st_size)
        return -1;
    return 1;
}

static int collect_records(FILE *fp, record_ref **refs_out, size_t *count_out)
{
    record_ref *refs = NULL;
    size_t count = 0;
    size_t capacity = 0;
    disk_record record;
    off_t offset;
    int status;

    rewind(fp);
    while ((status = read_record(fp, &record, &offset)) > 0) {
        if (count == capacity) {
            size_t next = capacity == 0 ? 16 : capacity * 2;
            record_ref *grown = realloc(refs, next * sizeof(*refs));
            if (grown == NULL) {
                free(refs);
                return -1;
            }
            refs = grown;
            capacity = next;
        }
        refs[count].record = record;
        refs[count].offset = offset;
        ++count;
    }
    if (status < 0) {
        free(refs);
        return -1;
    }
    *refs_out = refs;
    *count_out = count;
    return 0;
}

static int read_payload(FILE *fp, const disk_record *record, crash_info *info,
                        char *result, char *client_ip)
{
    memset(info, 0, sizeof(*info));
    info->signo = record->signo;
    info->exit_code = record->exit_code;
    info->timed_out = record->timed_out;
    info->elapsed_ms = (long)record->elapsed_ms;
    info->maxrss_kb = (long)record->maxrss_kb;
    if (fread(info->cmd, 1, record->cmd_len, fp) != record->cmd_len ||
        fread(info->last_output, 1, record->last_output_len, fp) != record->last_output_len ||
        fread(result, 1, record->result_len, fp) != record->result_len ||
        fread(client_ip, 1, record->client_ip_len, fp) != record->client_ip_len)
        return -1;
    info->cmd[record->cmd_len] = '\0';
    info->last_output[record->last_output_len] = '\0';
    result[record->result_len] = '\0';
    client_ip[record->client_ip_len] = '\0';
    return 0;
}

int store_save(const crash_info *in, const char *result, const char *client_ip)
{
    FILE *fp;
    record_ref *refs = NULL;
    size_t count = 0;
    disk_record record;
    int id = -1;
    size_t cmd_len, output_len, result_len, ip_len;

    if (in == NULL || result == NULL || client_ip == NULL ||
        (cmd_len = strnlen(in->cmd, sizeof(in->cmd))) >= sizeof(in->cmd) ||
        (output_len = strnlen(in->last_output, sizeof(in->last_output))) >= sizeof(in->last_output) ||
        (result_len = strnlen(result, STORE_RESULT_MAX + 1U)) > STORE_RESULT_MAX ||
        (ip_len = strnlen(client_ip, 64)) >= 64)
        return -1;

    pthread_mutex_lock(&store_lock);
    if (mkdir(STORE_DIR, 0755) != 0 && errno != EEXIST)
        goto done;
    fp = fopen(STORE_PATH, "a+b");
    if (fp == NULL)
        goto done;
    if (collect_records(fp, &refs, &count) != 0 ||
        (count > 0 && refs[count - 1].record.id >= INT32_MAX)) {
        free(refs);
        fclose(fp);
        goto done;
    }
    memset(&record, 0, sizeof(record));
    record.magic = STORE_MAGIC;
    record.version = STORE_VERSION;
    record.id = count == 0 ? 1 : refs[count - 1].record.id + 1;
    record.signo = in->signo;
    record.exit_code = in->exit_code;
    record.timed_out = in->timed_out;
    record.elapsed_ms = in->elapsed_ms;
    record.maxrss_kb = in->maxrss_kb;
    record.cmd_len = (uint32_t)cmd_len;
    record.last_output_len = (uint32_t)output_len;
    record.result_len = (uint32_t)result_len;
    record.client_ip_len = (uint32_t)ip_len;
    free(refs);
    if (fseeko(fp, 0, SEEK_END) != 0 ||
        fwrite(&record, sizeof(record), 1, fp) != 1 ||
        fwrite(in->cmd, 1, cmd_len, fp) != cmd_len ||
        fwrite(in->last_output, 1, output_len, fp) != output_len ||
        fwrite(result, 1, result_len, fp) != result_len ||
        fwrite(client_ip, 1, ip_len, fp) != ip_len || fflush(fp) != 0 ||
        fsync(fileno(fp)) != 0) {
        fclose(fp);
        goto done;
    }
    id = (int)record.id;
    fclose(fp);
done:
    pthread_mutex_unlock(&store_lock);
    return id;
}

int store_list(int n, char *out, size_t outsz)
{
    FILE *fp;
    record_ref *refs = NULL;
    size_t count = 0, shown, i, used = 0;
    int failed = -1;

    if (n <= 0 || out == NULL || outsz == 0)
        return -1;
    pthread_mutex_lock(&store_lock);
    fp = fopen(STORE_PATH, "rb");
    if (fp == NULL) {
        if (errno == ENOENT) {
            int written = snprintf(out, outsz, "저장된 기록이 없습니다.\n");
            failed = written >= 0 && (size_t)written < outsz ? 0 : -1;
        }
        goto done;
    }
    if (collect_records(fp, &refs, &count) != 0)
        goto close_done;
    if (count == 0) {
        int written = snprintf(out, outsz, "저장된 기록이 없습니다.\n");
        failed = written >= 0 && (size_t)written < outsz ? 0 : -1;
        goto close_done;
    }
    shown = (size_t)n < count ? (size_t)n : count;
    for (i = count; i > count - shown; --i) {
        disk_record *r = &refs[i - 1].record;
        char cmd[sizeof(((crash_info *)0)->cmd) + 1];
        size_t need = r->cmd_len > 200 ? 200 : r->cmd_len;
        int written;
        if (fseeko(fp, refs[i - 1].offset + (off_t)sizeof(*r), SEEK_SET) != 0 ||
            fread(cmd, 1, need, fp) != need)
            goto close_done;
        cmd[need] = '\0';
        written = snprintf(out + used, outsz - used,
                           "#%u | %s | signal=%d exit=%d timeout=%s | %" PRId64 " ms\n",
                           r->id, cmd, r->signo, r->exit_code,
                           r->timed_out ? "yes" : "no", r->elapsed_ms);
        if (written < 0 || (size_t)written >= outsz - used)
            goto close_done;
        used += (size_t)written;
    }
    failed = 0;
close_done:
    free(refs);
    fclose(fp);
done:
    pthread_mutex_unlock(&store_lock);
    return failed;
}

int store_show(int id, char *out, size_t outsz)
{
    FILE *fp;
    record_ref *refs = NULL;
    size_t count = 0, i;
    int failed = -1;

    if (id <= 0 || out == NULL || outsz == 0)
        return -1;
    pthread_mutex_lock(&store_lock);
    fp = fopen(STORE_PATH, "rb");
    if (fp == NULL)
        goto done;
    if (collect_records(fp, &refs, &count) != 0)
        goto close_done;
    for (i = 0; i < count; ++i) {
        if (refs[i].record.id == (uint32_t)id) {
            disk_record *r = &refs[i].record;
            crash_info info;
            char *result = malloc((size_t)r->result_len + 1);
            char client_ip[64];
            int written;
            if (result == NULL || fseeko(fp, refs[i].offset + (off_t)sizeof(*r), SEEK_SET) != 0 ||
                read_payload(fp, r, &info, result, client_ip) != 0) {
                free(result);
                goto close_done;
            }
            written = snprintf(out, outsz,
                "기록 #%u\n명령: %s\n클라이언트: %s\n시그널: %d\n종료 코드: %d\n"
                "시간 초과: %s\n실행 시간: %ld ms\n최대 메모리: %ld KB\n"
                "마지막 출력:\n%s\n\n분석 결과:\n%s\n",
                r->id, info.cmd, client_ip, info.signo, info.exit_code,
                info.timed_out ? "예" : "아니오", info.elapsed_ms, info.maxrss_kb,
                info.last_output[0] ? info.last_output : "(출력 정보 없음)", result);
            free(result);
            if (written >= 0 && (size_t)written < outsz)
                failed = 0;
            goto close_done;
        }
    }
close_done:
    free(refs);
    fclose(fp);
done:
    pthread_mutex_unlock(&store_lock);
    return failed;
}
