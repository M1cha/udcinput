/*
 * Copyright (c) 2026 Michael Zimmermann
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <udcinput.h>

#include <udcinput/buf_p.h>
#include <udcinput/util.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <unistd.h>

#define LOG_TAG "udcinput"
#include <udcinput/log_p.h>

static bool is_gadget_name_valid(const char *name)
{
	while (*name) {
		if (!isalnum(*name)) {
			return false;
		}
		name++;
	}

	return true;
}

static void delete_config_contents(DIR *parentdir, const struct dirent *config_dirent)
{
	int configdirfd = openat(dirfd(parentdir), config_dirent->d_name, O_RDONLY | O_DIRECTORY);
	if (!configdirfd) {
		LOG_ERR("failed to open config dir %s: %s", config_dirent->d_name, strerror(errno));
		return;
	}

	/* Delete the function symlinks. */

	DIR *dir = udcinput_opendirat(configdirfd, ".");
	if (!dir) {
		LOG_ERR("failed to open config dir %s: %s", config_dirent->d_name, strerror(errno));
		goto out_close_configdirfd;
	}

	struct dirent *dirent;
	while ((dirent = udcinput_readdir(dir))) {
		if (dirent->d_type == DT_LNK) {
			udcinput_try_unlink(dir, dirent, 0);
		}
	}
	closedir(dir);

	/* Delete the strings. */

	dir = udcinput_opendirat(configdirfd, "strings");
	if (!dir) {
		LOG_ERR("failed to open strings in config dir %s: %s", config_dirent->d_name,
			strerror(errno));
		goto out_close_configdirfd;
	}

	while ((dirent = udcinput_readdir(dir))) {
		udcinput_try_unlink(dir, dirent, AT_REMOVEDIR);
	}
	closedir(dir);

out_close_configdirfd:
	close(configdirfd);
}

static void delete_sysfs_gadget(const char *path)
{
	int ret;

	if (path == NULL) {
		return;
	}

	int dirfd = open(path, O_RDONLY | O_DIRECTORY);
	if (dirfd < 0) {
		if (errno == ENOENT) {
			return;
		}

		LOG_ERR("Failed to open gadget directory at %s: %s", path, strerror(errno));
		return;
	}

	udcinput_write_string(path, "UDC", "\n");

	DIR *dir = udcinput_opendirat(dirfd, "configs");
	if (dir) {
		struct dirent *dirent;
		while ((dirent = udcinput_readdir(dir))) {
			delete_config_contents(dir, dirent);
			udcinput_try_unlink(dir, dirent, AT_REMOVEDIR);
		}

		closedir(dir);
	}

	dir = udcinput_opendirat(dirfd, "functions");
	if (dir) {
		struct dirent *dirent;
		while ((dirent = udcinput_readdir(dir))) {
			udcinput_try_unlink(dir, dirent, AT_REMOVEDIR);
		}

		closedir(dir);
	}

	dir = udcinput_opendirat(dirfd, "strings");
	if (dir) {
		struct dirent *dirent;
		while ((dirent = udcinput_readdir(dir))) {
			udcinput_try_unlink(dir, dirent, AT_REMOVEDIR);
		}

		closedir(dir);
	}

	ret = rmdir(path);
	if (ret < 0) {
		LOG_ERR("Failed to delete gadget directory at %s: %s", path, strerror(errno));
	}

	close(dirfd);
}

void udcinput_gadget_create(struct udcinput_gadget *gadget)
{
	*gadget = (struct udcinput_gadget){
		.next_function_id = 1,
	};
}

int udcinput_gadget_init(struct udcinput_gadget *gadget, const char *configdir, const char *name,
			 bool cleanup)
{
	if (gadget->path) {
		LOG_WRN("Tried to reinitialize gadget: %s", gadget->path);
		return -EALREADY;
	}
	if (!is_gadget_name_valid(name)) {
		LOG_ERR("gadget name is not alphanumeric: %s", name);
		return -1;
	}

	char *path = udcinput_sprintf_alloc("%s/usb_gadget/%s", configdir, name);
	if (path == NULL) {
		return -ENOMEM;
	}

	if (cleanup) {
		delete_sysfs_gadget(path);
	}

	int ret = mkdir(path, 0755);
	if (ret < 0) {
		ret = -errno;
		LOG_ERR("Failed to create gadget %s: %s", name, strerror(errno));
		free(path);
		return ret;
	}

	gadget->path = path;

	return 0;
}

void udcinput_gadget_destroy(struct udcinput_gadget *gadget)
{
	delete_sysfs_gadget(gadget->path);
	free(gadget->path);
}

int udcinput_gadget_enable(struct udcinput_gadget *gadget, const char *udc_name)
{
	if (gadget->path == NULL) {
		LOG_WRN("Tried to enable uninitialized gadget");
		return -EPERM;
	}

	int ret = udcinput_write_string(gadget->path, "UDC", udc_name);
	if (ret < 0) {
		ret = -errno;
		LOG_ERR("Failed to enable gadget %s: %s", gadget->path, strerror(errno));
		return ret;
	}

	gadget->enabled = true;

	return 0;
}

int udcinput_gadget_disable(struct udcinput_gadget *gadget)
{
	if (gadget->path == NULL) {
		LOG_WRN("Tried to disable uninitialized gadget");
		return -EPERM;
	}

	int ret = udcinput_write_string(gadget->path, "UDC", "\n");
	if (ret < 0) {
		ret = -errno;
		LOG_ERR("Failed to disable gadget %s: %s", gadget->path, strerror(errno));
		return ret;
	}
	return 0;
}

int udcinput_function_create(struct udcinput_function *function, struct udcinput_gadget *gadget,
			     const char *name)
{
	if (gadget->path == NULL) {
		LOG_WRN("Tried to initialize function with uninitialized gadget");
		return -EPERM;
	}

	const size_t function_id = gadget->next_function_id;
	char *path = udcinput_sprintf_alloc("%s/functions/%s.%zu", gadget->path, name, function_id);
	if (path == NULL) {
		return -ENOMEM;
	}

	int ret = mkdir(path, 0755);
	if (ret < 0) {
		ret = -errno;
		LOG_ERR("Failed to create function %s: %s", path, strerror(errno));
		free(path);
		return ret;
	}

	gadget->next_function_id += 1;

	*function = (struct udcinput_function){
		.path = path,
		.id = function_id,
	};

	return 0;
}

void udcinput_function_destroy(struct udcinput_function *function)
{
	if (function->symlink_path) {
		int ret = unlink(function->symlink_path);
		if (ret < 0) {
			LOG_ERR("Failed to delete function symlink at %s: %s",
				function->symlink_path, strerror(errno));
		}
		free(function->symlink_path);
	}

	int ret = rmdir(function->path);
	if (ret < 0) {
		LOG_ERR("Failed to delete function directory at %s: %s", function->path,
			strerror(errno));
	}
	free(function->path);
}

int udcinput_function_enable(struct udcinput_function *function, const char *config)
{
	if (function->symlink_path) {
		LOG_WRN("Function is already enabled");
		return -EALREADY;
	}

	char *symlink_path = udcinput_sprintf_alloc("%s/../../configs/%s/%zu", function->path,
						    config, function->id);
	if (symlink_path == NULL) {
		return -ENOMEM;
	}

	int ret = symlink(function->path, symlink_path);
	if (ret < 0) {
		ret = -errno;
		LOG_ERR("Failed to enable function %s for config %s: %s", function->path,
			symlink_path, strerror(errno));
		free(symlink_path);
		return ret;
	}

	function->symlink_path = symlink_path;

	return 0;
}

int udcinput_loop_create(struct udcinput_loop *loop)
{
	int ret;

	const int epollfd = epoll_create(2);
	if (epollfd < 0) {
		ret = -errno;
		LOG_ERR("Failed to create epoll: %s", strerror(errno));
		return ret;
	}

	const int stopfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (epollfd < 0) {
		ret = -errno;
		LOG_ERR("Failed to create stop eventfd: %s", strerror(errno));
		goto err_close_epollfd;
	}

	*loop = (struct udcinput_loop){
		.epollfd = epollfd,
		.stopfd = stopfd,
		.datafd = -1,
	};

	struct epoll_event event = {
		.events = EPOLLIN,
		.data.fd = loop->stopfd,
	};

	ret = epoll_ctl(loop->epollfd, EPOLL_CTL_ADD, event.data.fd, &event);
	if (ret < 0) {
		ret = -errno;
		LOG_ERR("Failed to add stopfd to epoll: %s", strerror(errno));
		goto err_close_stopfd;
	}

	return 0;

err_close_stopfd:
	close(stopfd);
err_close_epollfd:
	close(epollfd);

	return ret;
}

void udcinput_loop_destroy(struct udcinput_loop *loop)
{
	int ret = epoll_ctl(loop->epollfd, EPOLL_CTL_DEL, loop->stopfd, NULL);
	if (ret < 0) {
		LOG_WRN("Failed to remove stopfd from epoll: %s", strerror(errno));
	}
	close(loop->stopfd);

	if (loop->datafd > 0) {
		ret = epoll_ctl(loop->epollfd, EPOLL_CTL_DEL, loop->datafd, NULL);
		if (ret < 0) {
			LOG_WRN("Failed to remove datafd from epoll: %s", strerror(errno));
		}
	}

	close(loop->epollfd);
}

static void open_datafd(struct udcinput_loop *loop, void *user_data,
			const struct udcinput_loop_callbacks *callbacks)
{
	if (loop->datafd >= 0) {
		return;
	}

	/* We don't want this to be non-blocking, so the gamepad implementation
	 * can write synchronously.
	 */
	const int datafd = callbacks->open(user_data);
	if (datafd < 0) {
		return;
	}
	loop->datafd = datafd;

	LOG_INF("opened datafd");

	struct epoll_event event = {
		.events = EPOLLIN,
		.data.fd = loop->datafd,
	};
	int ret = epoll_ctl(loop->epollfd, EPOLL_CTL_ADD, event.data.fd, &event);
	if (ret < 0) {
		LOG_ERR("Failed to add hidgfd to epoll: %s", strerror(errno));
		loop->datafd = -1;
		callbacks->close(user_data);
		return;
	}
}

static void close_datafd(struct udcinput_loop *loop, void *user_data,
			 const struct udcinput_loop_callbacks *callbacks)
{
	if (loop->datafd < 0) {
		return;
	}

	int ret = epoll_ctl(loop->epollfd, EPOLL_CTL_DEL, loop->datafd, NULL);
	if (ret < 0) {
		LOG_ERR("Failed to remove hidgfd from epoll: %s", strerror(errno));
		return;
	}

	callbacks->close(user_data);
	loop->datafd = -1;
}

static int process_datafd_events(struct udcinput_loop *loop, void *user_data,
				 const struct udcinput_loop_callbacks *callbacks)
{
	int ret;

	/* NOTE: the size is specific to switchpro, we might want to improve that. */
	/* NOTE: This is aligned to reduce the likelyhood of unaligned access being required. */
	uint8_t data[64] __aligned(sizeof(void *));
	ssize_t nbytes = read(loop->datafd, data, sizeof(data));
	if (nbytes < 0) {
		ret = -errno;
		LOG_WRN("Failed to read from datafd: %s", strerror(errno));
		return ret;
	}

	struct udcinput_buf buf = udcinput_buf_create(data, sizeof(data));
	buf.size = nbytes;

	callbacks->on_data(user_data, loop->datafd, &buf);

	return 0;
}

int udcinput_loop_run(struct udcinput_loop *loop, void *user_data,
		      const struct udcinput_loop_callbacks *callbacks)
{
	int loop_ret = 0;
	int ret;
	const int setup_timeout = 500;
	int timeout = setup_timeout;

	struct epoll_event event;
	while (true) {
		int nfds = epoll_wait(loop->epollfd, &event, 1, timeout);
		if (nfds < 0) {
			loop_ret = -errno;
			LOG_ERR("Failed to wait for epoll events: %s", strerror(errno));
			break;
		}

		if (nfds < 1) {
			open_datafd(loop, user_data, callbacks);
			if (loop->datafd >= 0) {
				timeout = -1;
			}
			continue;
		}

		if (event.data.fd == loop->datafd) {
			if (event.events & EPOLLHUP) {
				LOG_WRN("datafd has an epoll hup");
				close_datafd(loop, user_data, callbacks);
				timeout = setup_timeout;
				continue;
			}
			if (event.events & EPOLLERR) {
				LOG_WRN("datafd has an epoll error");
				close_datafd(loop, user_data, callbacks);
				timeout = setup_timeout;
				continue;
			}
			if (event.events & EPOLLIN) {
				ret = process_datafd_events(loop, user_data, callbacks);
				if (ret < 0) {
					close_datafd(loop, user_data, callbacks);
					timeout = setup_timeout;
					continue;
				}
			}
		} else if (event.data.fd == loop->stopfd) {
			if (event.events & EPOLLERR) {
				LOG_ERR("eventfd has an epoll error");
				loop_ret = -EIO;
				break;
			}
			if (event.events & EPOLLHUP) {
				LOG_ERR("eventfd has an epoll hup");
				loop_ret = -EIO;
				break;
			}
			if (event.events & EPOLLIN) {
				LOG_DBG("Stop event reveived");
				/* We don't read from it, so the loop will return
				 * even if we were to poll it again.
				 */
				break;
			}
		}
	}

	return loop_ret;
}

int udcinput_loop_stop(struct udcinput_loop *loop)
{
	int ret;

	const uint64_t value = 1;
	const ssize_t nwritten = write(loop->stopfd, &value, sizeof(value));
	if (nwritten < 0) {
		if (errno == EAGAIN) {
			/* If this happens, we had lots of calls to stop already,
			 * because the counter is at 0xfffffffffffffffe.
			 */
			return 0;
		}

		ret = -errno;
		LOG_ERR("Failed to stop loop: %s", strerror(errno));
		return ret;
	}

	return 0;
}
