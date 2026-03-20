/*
 * Copyright (c) 2026 Michael Zimmermann
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef UDCINPUT_LOG_P_H
#define UDCINPUT_LOG_P_H

#include <udcinput/log.h>

#include <stddef.h>

#define LOG_LEVEL_NONE 0
#define LOG_LEVEL_ERR  1
#define LOG_LEVEL_WRN  2
#define LOG_LEVEL_INF  3
#define LOG_LEVEL_DBG  4

#ifndef LOG_TAG
#define LOG_TAG NULL
#endif

/* This allows overriding the level per file for development. */
#ifndef LOG_LEVEL
#define LOG_LEVEL UDCINPUT_LOG_LEVEL
#endif

void udcinput_hexdump(uint8_t level, const char *tag, const void *data, size_t len);

#define _LOG_CHECK_LEVEL(level)                                                                    \
	if (UDCINPUT_LOG_LEVEL_##level > (LOG_LEVEL)) {                                            \
		break;                                                                             \
	}

#define _LOG_IMPL(level, fmt, ...)                                                                 \
	do {                                                                                       \
		_LOG_CHECK_LEVEL(level);                                                           \
		udcinput_write_log(UDCINPUT_LOG_LEVEL_##level, LOG_TAG, fmt, ##__VA_ARGS__);       \
	} while (0)

#define LOG_ERR(fmt, ...) _LOG_IMPL(ERR, fmt, ##__VA_ARGS__)
#define LOG_WRN(fmt, ...) _LOG_IMPL(WRN, fmt, ##__VA_ARGS__)
#define LOG_INF(fmt, ...) _LOG_IMPL(INF, fmt, ##__VA_ARGS__)
#define LOG_DBG(fmt, ...) _LOG_IMPL(DBG, fmt, ##__VA_ARGS__)

#define _LOG_HEXDUMP_IMPL(level, data, len)                                                        \
	do {                                                                                       \
		_LOG_CHECK_LEVEL(level);                                                           \
		udcinput_hexdump(UDCINPUT_LOG_LEVEL_##level, LOG_TAG, (data), (len));              \
	} while (0)

#define LOG_HEXDUMP_ERR(data, len) _LOG_HEXDUMP_IMPL(ERR, (data), (len))
#define LOG_HEXDUMP_WRN(data, len) _LOG_HEXDUMP_IMPL(WRN, (data), (len))
#define LOG_HEXDUMP_INF(data, len) _LOG_HEXDUMP_IMPL(INF, (data), (len))
#define LOG_HEXDUMP_DBG(data, len) _LOG_HEXDUMP_IMPL(DBG, (data), (len))

#endif /* UDCINPUT_LOG_P_H */
