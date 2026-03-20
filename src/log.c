/*
 * Copyright (c) 2026 Michael Zimmermann
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <udcinput/log.h>

#include <udcinput/buf_p.h>
#include <udcinput/log_p.h>
#include <stdarg.h>
#include <stdio.h>

static const char *level_to_str(uint8_t level)
{
	switch (level) {
	case UDCINPUT_LOG_LEVEL_ERR:
		return "ERR";
	case UDCINPUT_LOG_LEVEL_WRN:
		return "WRN";
	case UDCINPUT_LOG_LEVEL_INF:
		return "INF";
	case UDCINPUT_LOG_LEVEL_DBG:
		return "DBG";
	default:
		return "UNK";
	}
}

__attribute__((__weak__)) void udcinput_write_log(uint8_t level, const char *tag, const char *fmt,
						  ...)
{
	fprintf(stderr, "[%s] ", level_to_str(level));

	if (tag) {
		fprintf(stderr, "%s: ", tag);
	}

	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	fprintf(stderr, "\n");
}

void udcinput_hexdump(uint8_t level, const char *tag, const void *const data_, size_t len)
{
	const uint8_t *const data = data_;
	const size_t bytes_per_line = 16;
	UDCINPUT_BUF_DEFINE(linebuf, bytes_per_line * 3);

	for (size_t i = 0; i < len; i += bytes_per_line) {
		udcinput_buf_reset(&linebuf);

		size_t remaining = len - i;
		size_t count = MIN(remaining, bytes_per_line);

		int ret = 0;
		for (size_t j = 0; ret == 0 && j < count; j++) {
			if (j == 0) {
				ret = udcinput_buf_add_printf(&linebuf, "%02X", data[i + j]);
			} else {
				ret = udcinput_buf_add_printf(&linebuf, " %02X", data[i + j]);
			}
		}

		if (ret == 0 && udcinput_buf_add_u8(&linebuf, 0) == 0) {
			udcinput_write_log(level, tag, "%s", linebuf.data);
		} else {
			udcinput_write_log(level, tag, "** HEXDUMP OVERFLOW **");
		}
	}
}
