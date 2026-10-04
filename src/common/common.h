#ifndef HTSN_COMMON_H
#define HTSN_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define HTSN_MAX_STR 256
#define HTSN_MAX_DEVICES 512
#define HTSN_MAX_TOPICS 128

typedef enum {
    HTSN_OK = 0,
    HTSN_ERR_INVALID_ARG,
    HTSN_ERR_NO_MEMORY,
    HTSN_ERR_NOT_FOUND,
    HTSN_ERR_ALREADY_EXISTS,
    HTSN_ERR_DB,
    HTSN_ERR_IO,
    HTSN_ERR_NET,
    HTSN_ERR_NOT_IMPLEMENTED,
    HTSN_ERR_BUSY,
    HTSN_ERR_NOT_READY,
    HTSN_ERR_LAST
} htsn_error;

static inline const char *htsn_error_str(htsn_error e) {
    switch (e) {
    case HTSN_OK: return "ok";
    case HTSN_ERR_INVALID_ARG: return "invalid argument";
    case HTSN_ERR_NO_MEMORY: return "out of memory";
    case HTSN_ERR_NOT_FOUND: return "not found";
    case HTSN_ERR_ALREADY_EXISTS: return "already exists";
    case HTSN_ERR_DB: return "database error";
    case HTSN_ERR_IO: return "i/o error";
    case HTSN_ERR_NET: return "network error";
    case HTSN_ERR_NOT_IMPLEMENTED: return "not implemented";
    case HTSN_ERR_BUSY: return "busy";
    case HTSN_ERR_NOT_READY: return "not ready";
    default: return "unknown error";
    }
}

#endif
