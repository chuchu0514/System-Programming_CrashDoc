#include "analyzer.h"
#include "analyzer_internal.h"

#include <stdio.h>
#include <stdlib.h>

static int llm_enabled = 1;

void analyzer_init(int use_llm)
{
    llm_enabled = use_llm != 0;
}

/* Keep both independent explanations visible when both analyzers succeed. */
static int combine_results(const char *llm, int llm_ok,
                           const char *rule, int rule_ok,
                           const char *rule_only_reason,
                           char *out, size_t outsz)
{
    int n;

    if (llm_ok && rule_ok)
        n = snprintf(out, outsz,
                     "[분석 방식: 규칙 기반 + LLM 통합]\n%s",
                     llm);
    else if (llm_ok)
        n = snprintf(out, outsz, "[분석 방식: LLM]\n%s", llm);
    else if (rule_ok)
        n = snprintf(out, outsz,
                     "[분석 방식: 규칙 기반만 사용]\n사유: %s\n\n%s",
                     rule_only_reason, rule);
    else
        return -1;
    return n >= 0 && (size_t)n < outsz ? 0 : -1;
}

int analyze_crash(const crash_info *in, char *out, size_t outsz)
{
    char *llm = NULL;
    char *rule = NULL;
    int llm_ok = 0, rule_ok;
    int result;

    if (in == NULL || out == NULL || outsz == 0)
        return -1;
    rule = malloc(outsz);
    if (rule == NULL)
        return -1;
    rule_ok = rule_analyze_crash(in, rule, outsz) == 0;
    if (llm_enabled && in->signo != 2) {
        llm = malloc(outsz);
        if (llm != NULL)
            llm_ok = llm_analyze_crash(in, rule_ok ? rule : "(규칙 분석 실패)",
                                       llm, outsz) == 0;
    }
    result = combine_results(llm, llm_ok, rule, rule_ok,
                             !llm_enabled
                             ? "LLM이 비활성화되어 있습니다 (--no-llm)."
                             : in->signo == 2
                             ? "SIGINT(일반적으로 Ctrl+C)는 규칙 기반으로 처리합니다."
                             : "LLM 검토를 완료하지 못해 규칙 결과만 표시합니다.",
                             out, outsz);
    free(llm);
    free(rule);
    return result;
}

int analyze_build_with_source(const char *error_text, const char *source_code,
                              char *out, size_t outsz)
{
    char *llm = NULL;
    char *rule = NULL;
    int llm_ok = 0, rule_ok;
    int result;

    if (error_text == NULL || source_code == NULL || out == NULL || outsz == 0)
        return -1;
    rule = malloc(outsz);
    if (rule == NULL)
        return -1;
    rule_ok = rule_analyze_build(error_text, rule, outsz) == 0;

    if (!llm_enabled) {
        result = rule_ok
            ? snprintf(out, outsz,
                       "[분석 방식: 규칙 기반만 사용]\n사유: LLM이 비활성화되어 있습니다 (--no-llm).\n\n%s",
                       rule)
            : -1;
        free(rule);
        return result >= 0 && (size_t)result < outsz ? 0 : -1;
    }

    llm = malloc(outsz);
    if (llm != NULL)
        llm_ok = llm_analyze_build(error_text, rule_ok ? rule : "(규칙 분석 실패)",
                                   source_code,
                                   llm, outsz) == 0;
    result = combine_results(llm, llm_ok, rule, rule_ok,
                             "LLM 검토를 완료하지 못해 규칙 결과만 표시합니다.",
                             out, outsz);
    free(llm);
    free(rule);
    return result;
}

int analyze_build(const char *error_text, char *out, size_t outsz)
{
    return analyze_build_with_source(error_text, "", out, outsz);
}
