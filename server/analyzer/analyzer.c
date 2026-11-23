#include "analyzer.h"
#include "analyzer_internal.h"

static int llm_enabled = 1;

void analyzer_init(int use_llm)
{
    llm_enabled = use_llm != 0;
}

int analyze_crash(const crash_info *in, char *out, size_t outsz)
{
    if (in == NULL || out == NULL || outsz == 0)
        return -1;
    if (llm_enabled && llm_analyze_crash(in, out, outsz) == 0)
        return 0;
    return rule_analyze_crash(in, out, outsz);
}

int analyze_build(const char *error_text, char *out, size_t outsz)
{
    if (error_text == NULL || out == NULL || outsz == 0)
        return -1;
    if (llm_enabled && llm_analyze_build(error_text, out, outsz) == 0)
        return 0;
    return rule_analyze_build(error_text, out, outsz);
}
