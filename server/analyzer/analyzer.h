#ifndef ANALYZER_H
#define ANALYZER_H

#include <stddef.h>
#include "crash_info.h"

void analyzer_init(int use_llm);
int analyze_crash(const crash_info *in, char *out, size_t outsz);
int analyze_build(const char *error_text, char *out, size_t outsz);

#endif
