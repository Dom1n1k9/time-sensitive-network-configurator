#include "common/log.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static htsn_log_level g_level = HTSN_LOG_INFO;
static FILE *g_file = NULL;
static bool g_use_stderr = true;

static const char *level_name(htsn_log_level l) {
    switch (l) {
    case HTSN_LOG_DEBUG: return "DEBUG";
    case HTSN_LOG_INFO:  return "INFO";
    case HTSN_LOG_WARN:  return "WARN";
    case HTSN_LOG_ERROR: return "ERROR";
    default: return "?";
    }
}

void htsn_log_init(htsn_log_level level, const char *file) {
    g_level = level;
    if (file) {
        htsn_log_to_file(file);
    }
}

void htsn_log_to_file(const char *path) {
    if (g_file) fclose(g_file);
    g_file = fopen(path, "a");
    g_use_stderr = (g_file == NULL);
}

void htsn_log_set_level(htsn_log_level level) {
    g_level = level;
}

void htsn_log(htsn_log_level level, const char *fmt, ...) {
    if ((int)level < (int)g_level) return;

    char ts[32];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (tm) {
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);
    } else {
        ts[0] = '?'; ts[1] = '\0';
    }

    FILE *out = g_use_stderr ? stderr : g_file;
    fprintf(out, "[%s] [%s] ", ts, level_name(level));

    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);

    fputc('\n', out);
    fflush(out);
}
