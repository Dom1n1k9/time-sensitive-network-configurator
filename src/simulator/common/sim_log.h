#ifndef SIM_LOG_H
#define SIM_LOG_H

/* The simulator reuses the core log implementation (src/common/log.c).
 * sim_* names are kept as aliases so simulator sources stay unchanged. */
#include "common/log.h"

#define SIM_LOG_DEBUG HTSN_LOG_DEBUG
#define SIM_LOG_INFO  HTSN_LOG_INFO
#define SIM_LOG_WARN  HTSN_LOG_WARN
#define SIM_LOG_ERROR HTSN_LOG_ERROR

typedef htsn_log_level sim_log_level;
#define sim_log        htsn_log
#define sim_log_init   htsn_log_init

#endif
