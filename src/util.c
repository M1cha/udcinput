/*
 * Copyright (c) 2026 Michael Zimmermann
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <udcinput/util.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define LOG_TAG "util"
#include <udcinput/log_p.h>

char *udcinput_vsprintf_alloc(const char *fmt, va_list ap)
{
	va_list ap_copy;
	va_copy(ap_copy, ap);
	const int nbytes = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap_copy);

	if (nbytes <= 0) {
		LOG_ERR("BUG: failed to calculate buffer size");
		return NULL;
	}

	char *buf = malloc(nbytes + 1);
	if (!buf) {
		LOG_ERR("failed to allocate snprintf memory");
		return NULL;
	}

	const int nwritten = vsnprintf(buf, nbytes + 1, fmt, ap);

	if (nwritten < 0 || nwritten >= nbytes + 1) {
		LOG_ERR("BUG: failed to format string");
		free(buf);
		return NULL;
	}

	return buf;
}

char *udcinput_sprintf_alloc(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	char *buf = udcinput_vsprintf_alloc(fmt, ap);
	va_end(ap);

	return buf;
}

int udcinput_try_unlink(DIR *dir, struct dirent *dirent, int flags)
{
	int ret = unlinkat(dirfd(dir), dirent->d_name, flags);
	if (ret < 0) {
		ret = -errno;
		LOG_WRN("can't delete %s: %s", dirent->d_name, strerror(errno));
		return ret;
	}

	return 0;
}

DIR *udcinput_opendirat(int dirfd, const char *path)
{
	int subdirfd = openat(dirfd, path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (subdirfd < 0) {
		LOG_ERR("failed to openat %s: %s", path, strerror(errno));
		return NULL;
	}

	DIR *dir = fdopendir(subdirfd);
	if (!dir) {
		LOG_ERR("failed to fdopendir %s: %s", path, strerror(errno));
		close(subdirfd);
		return NULL;
	}

	return dir;
}

struct dirent *udcinput_readdir(DIR *dirp)
{
	while (true) {
		errno = 0;
		struct dirent *dirent = readdir(dirp);
		if (errno) {
			LOG_ERR("readdir failed: %s", strerror(errno));
			return NULL;
		}
		if (!dirent) {
			return NULL;
		}

		if (!strcmp(dirent->d_name, ".")) {
			continue;
		}
		if (!strcmp(dirent->d_name, "..")) {
			continue;
		}

		return dirent;
	}
}

int udcinput_write_buf(const char *dirpath, const char *filepath, const void *buf, size_t bufsz)
{
	int ret;

	const int dirfd = open(dirpath, O_RDONLY | O_DIRECTORY);
	if (dirfd < 0) {
		ret = -errno;
		LOG_ERR("Failed to open %s: %s", dirpath, strerror(errno));
		return ret;
	}

	const int fd = openat(dirfd, filepath, O_WRONLY | O_TRUNC);
	close(dirfd);
	if (fd < 0) {
		ret = -errno;
		LOG_ERR("Failed to open %s: %s", filepath, strerror(errno));
		return ret;
	}

	ssize_t nwritten = write(fd, buf, bufsz);
	close(fd);
	if (nwritten < 0) {
		ret = -errno;
		LOG_ERR("Failed to write to %s: %s", filepath, strerror(errno));
		return ret;
	}
	if ((size_t)nwritten != bufsz) {
		LOG_WRN("BUG: short-write: wrote %zd instead of %zu bytes", nwritten, bufsz);
	}

	return 0;
}

int udcinput_write_u16(const char *dirpath, const char *filepath, uint16_t value)
{
	char buf[6];
	const int nbytes = snprintf(buf, sizeof(buf), "%u", value);
	if (nbytes < 0 || (size_t)nbytes >= sizeof(buf)) {
		LOG_ERR("BUG: failed to convert value to string");
		return -ENOMEM;
	}

	return udcinput_write_buf(dirpath, filepath, buf, nbytes);
}

int udcinput_write_string(const char *dirpath, const char *filepath, const char *value)
{
	return udcinput_write_buf(dirpath, filepath, value, strlen(value));
}

int udcinput_mkdir_fmt(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	char *path = udcinput_vsprintf_alloc(fmt, ap);
	va_end(ap);

	if (path == NULL) {
		return -ENOMEM;
	}

	int ret = mkdir(path, 0755);
	if (ret < 0) {
		ret = -errno;
		LOG_ERR("Failed to create %s: %s", path, strerror(errno));
	}

	free(path);
	return ret;
}

int udcinput_set_nonblocking(int fd)
{
	int ret = fcntl(fd, F_GETFL, 0);
	if (ret < 0) {
		ret = -errno;
		LOG_ERR("failed to get file status flags: %s", strerror(errno));
		return ret;
	}

	int flags = ret | O_NONBLOCK;

	ret = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	if (ret < 0) {
		ret = -errno;
		LOG_ERR("failed to set file status flags: %s", strerror(errno));
		return ret;
	}

	return 0;
}
