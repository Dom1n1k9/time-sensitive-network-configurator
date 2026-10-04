#ifndef HTSN_STR_UTIL_H
#define HTSN_STR_UTIL_H

#include "common/common.h"

#include <stddef.h>

size_t htsn_strlcpy(char *dst, const char *src, size_t size);
int htsn_str_valid_utf8(const char *s);
void htsn_str_trim(char *s);
bool htsn_str_starts_with(const char *s, const char *prefix);
char *htsn_str_dup(const char *s);

#endif
