#ifndef ANALYZER_INTERNAL_H
#define ANALYZER_INTERNAL_H

#include <stddef.h>
#include "crash_info.h"

int rule_analyze_crash(const crash_info *in, char *out, size_t outsz);
int rule_analyze_build(const char *error_text, char *out, size_t outsz);
int llm_analyze_crash(const crash_info *in, char *out, size_t outsz);
int llm_analyze_build(const char *error_text, char *out, size_t outsz);

#endif
