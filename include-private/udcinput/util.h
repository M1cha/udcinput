/*
 * Copyright (c) 2026 Michael Zimmermann
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * @brief Utility library.
 *
 * A lot of this was copied from the Zephyr Project:
 * https://github.com/zephyrproject-rtos/zephyr
 */

#ifndef UDCINPUT_UTIL_H
#define UDCINPUT_UTIL_H

#include <asm/bitsperlong.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdint.h>

/** @brief Maximum value a uint12 number can hold. */
#define UINT12_MAX (4095)

/**
 * @brief Unaligned access
 *
 * @param g Pointer to the data to read
 * @returns The Value behind @p g
 */
#define UNALIGNED_GET(g)                                                                           \
	__extension__({                                                                            \
		struct __attribute__((__packed__)) {                                               \
			typeof(*(g)) __v;                                                          \
		} *__g = (typeof(__g))(g);                                                         \
		__g->__v;                                                                          \
	})

#ifndef __aligned
#define __aligned(x) __attribute__((__aligned__(x)))
#endif

#ifndef MIN
/**
 * @brief Obtain the minimum of two values.
 *
 * @note Arguments are evaluated twice.
 *
 * @param a First value.
 * @param b Second value.
 *
 * @returns Minimum value of @p a and @p b.
 */
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
/**
 * @brief Obtain the maximum of two values.
 *
 * @note Arguments are evaluated twice.
 *
 * @param a First value.
 * @param b Second value.
 *
 * @returns Maximum value of @p a and @p b.
 */
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

/**
 * @brief Unsigned integer with bit position @p n set (signed in
 * assembly language).
 */
#define BIT(n) (1UL << (n))

/**
 * @brief Create a contiguous bitmask starting at bit position @p l
 *        and ending at position @p h.
 */
#define GENMASK(h, l) (((~0UL) - (1UL << (l)) + 1) & (~0UL >> (__BITS_PER_LONG - 1 - (h))))

/** @brief Extract the Least Significant Bit from @p value. */
#define LSB_GET(value) ((value) & -(value))

/**
 * @brief Extract a bitfield element from @p value corresponding to
 *	  the field mask @p mask.
 */
#define FIELD_GET(mask, value) (((value) & (mask)) / LSB_GET(mask))

/** @brief execute @p code when @p c evaluates to true. */
#define CHECK_EXEC(c, code)                                                                        \
	({                                                                                         \
		int ret_check = (c);                                                               \
		if (ret_check) {                                                                   \
			code;                                                                      \
		}                                                                                  \
	})

/** @brief returns @p c if it evaluates to true. */
#define CHECK(c) CHECK_EXEC((c), ({ return ret_check; }))

/** @brief returns if @p c evaluates to true. */
#define CHECKV(c) CHECK_EXEC((c)({ return; }))

/** @brief calls `goto check_fail_##label` if @p c evaluates to true. */
#define CHECKG(label, c) CHECK_EXEC((c), ({ goto check_fail_##label; }))

/** @brief Allocate memory and call vsprintf on it. */
char *udcinput_vsprintf_alloc(const char *fmt, va_list ap);

/** @brief Allocate memory and call sprintf on it. */
__attribute__((format(printf, 1, 2))) char *udcinput_sprintf_alloc(const char *fmt, ...);

/**
 * @brief Delete file @p dirent in directory @p dir.
 *
 * @param dir Directory stream which will be used to obtain it's fd.
 * @param dirent Directory entry which is used to obtain the file name.
 * @param flags Same as for the POSIX function `unlinkat`.
 */
int udcinput_try_unlink(DIR *dir, struct dirent *dirent, int flags);

/**
 * @brief Open directory @p path in directory @p dirfd.
 *
 * @param dirfd A file descriptor of a directory.
 * @param path Path to a file relative to @p dirfd
 *
 * @returns A Directory stream for use with POSIX APIs.
 */
DIR *udcinput_opendirat(int dirfd, const char *path);

/**
 * @brief A convencience wrapper around `readdir`
 *
 * With the following differences:
 * - skip . and ..
 * - reset and handle errno
 * - write log on errors.
 */
struct dirent *udcinput_readdir(DIR *dirp);

/** @brief Write @p bufsz bytes of @p buf to file @p filepath in directory @p dirpath. */
int udcinput_write_buf(const char *dirpath, const char *filepath, const void *buf, size_t bufsz);

/** @brief Format @p value as a string and write it to file @p filepath in directory @p dirpath. */
int udcinput_write_u16(const char *dirpath, const char *filepath, uint16_t value);

/** @brief Write @p value to file @p filepath in directory @p dirpath. */
int udcinput_write_string(const char *dirpath, const char *filepath, const char *value);

/** @brief Creates a path from the format arguments and calls `mkdir` on it. */
__attribute__((format(printf, 1, 2))) int udcinput_mkdir_fmt(const char *fmt, ...);

/** @brief Set O_NONBLOCK on @p fd. */
int udcinput_set_nonblocking(int fd);

#endif /* UDCINPUT_UTIL_H */
