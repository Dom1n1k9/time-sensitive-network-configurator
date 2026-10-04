#ifndef HTSN_LOG_H
#define HTSN_LOG_H

#include <stdarg.h>

typedef enum {
    HTSN_LOG_DEBUG = 0,
    HTSN_LOG_INFO,
    HTSN_LOG_WARN,
    HTSN_LOG_ERROR
} htsn_log_level;

void htsn_log_init(htsn_log_level level, const char *file);
void htsn_log_to_file(const char *path);
void htsn_log(htsn_log_level level, const char *fmt, ...);
void htsn_log_set_level(htsn_log_level level);

#endif
