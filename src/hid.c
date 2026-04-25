/*
 * Copyright (c) 2026 Michael Zimmermann
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <udcinput/hid.h>

#include <udcinput/util.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LOG_TAG "hid"
#include <udcinput/log_p.h>

static int lookup_hidg_path(const struct udcinput_function *function, char **out_path)
{
	int ret;

	char *devinfo_path = udcinput_sprintf_alloc("%s/dev", function->path);
	if (devinfo_path == NULL) {
		return -ENOMEM;
	}

	const int fd_devinfo = open(devinfo_path, O_RDONLY);
	free(devinfo_path);
	if (fd_devinfo < 0) {
		ret = -errno;
		LOG_ERR("Failed to open %s/dev: %s", function->path, strerror(errno));
		return ret;
	}

	char devinfo[10];
	ssize_t nread = read(fd_devinfo, devinfo, sizeof(devinfo));
	close(fd_devinfo);
	if (nread < 0) {
		ret = -errno;
		LOG_ERR("Failed to read from %s/dev: %s", function->path, strerror(errno));
		return ret;
	}
	if ((size_t)nread >= sizeof(devinfo)) {
		LOG_ERR("Contents of %s/dev are larger than %zu bytes", function->path,
			sizeof(devinfo) - 1);
		return -ENOMEM;
	}

	devinfo[nread] = 0;
	if (nread > 0 && devinfo[nread - 1] == '\n') {
		devinfo[nread - 1] = 0;
	}

	char *dev_path = udcinput_sprintf_alloc("/dev/char/%s", devinfo);
	if (dev_path == NULL) {
		return -ENOMEM;
	}

	*out_path = dev_path;
	return 0;
}

int udcinput_hidg_open(const struct udcinput_function *function)
{
	char *path = NULL;
	int ret = lookup_hidg_path(function, &path);
	if (ret) {
		return ret;
	}

	/* We don't want this to be non-blocking, so the gamepad implementation
	 * can write synchronously.
	 */
	const int hidgfd = open(path, O_RDWR, 0);
	if (hidgfd < 0) {
		if (errno == ENOENT) {
			return -ENOENT;
		}

		ret = -errno;
		LOG_ERR("Failed to open hidg at %s: %s", path, strerror(errno));
		free(path);
		return ret;
	}

	free(path);
	return hidgfd;
}

int udcinput_hidg_write_input_report(int fd, const struct udcinput_buf *buf)
{
	int ret;

	ssize_t nwritten = write(fd, buf->data, buf->size);
	if (nwritten < 0) {
		ret = -errno;
		if (errno != EAGAIN) {
			LOG_ERR("Failed to write input report: %s", strerror(errno));
		}
		return ret;
	}
	if ((size_t)nwritten != buf->size) {
		LOG_ERR("Wrote %zd instead of %zu bytes of input report", nwritten,
			(size_t)buf->size);
		return -EIO;
	}

	return 0;
}
