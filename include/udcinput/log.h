/*
 * Copyright (c) 2026 Michael Zimmermann
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef UDCINPUT_LOG_H
#define UDCINPUT_LOG_H

#include <stdint.h>

#define UDCINPUT_LOG_LEVEL_NONE 0
#define UDCINPUT_LOG_LEVEL_ERR  1
#define UDCINPUT_LOG_LEVEL_WRN  2
#define UDCINPUT_LOG_LEVEL_INF  3
#define UDCINPUT_LOG_LEVEL_DBG  4

/**
 * @brief Write a single log entry.
 *
 * udcinput provides a weak default implementation, which logs to stderr.
 * This can be replaced by the user of this library by simply implementing the
 * same function without weak linkage.
 */
__attribute__((format(printf, 3, 4))) void udcinput_write_log(uint8_t level, const char *tag,
							      const char *fmt, ...);

#endif /* UDCINPUT_H */
