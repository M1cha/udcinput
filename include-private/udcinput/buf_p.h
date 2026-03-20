/*
 * Copyright (c) 2026 Michael Zimmermann
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * @brief Buffer management.
 *
 * A lot of this was copied from the Zephyr Project:
 * https://github.com/zephyrproject-rtos/zephyr/blob/v4.3.0/include/zephyr/net_buf.h
 */

#ifndef UDCINPUT_BUF_P_H
#define UDCINPUT_BUF_P_H

#include <udcinput/buf.h>

#include <udcinput/util.h>
#include <endian.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

/**
 *  @brief Define a udcinput_buf stack variable.
 *
 *  This is a helper macro which is used to define a udcinput_buf object
 *  on the stack.
 *
 *  @param name_ Name of the udcinput_buf object.
 *  @param capacity_ Maximum data storage for the buffer.
 */
#define UDCINPUT_BUF_DEFINE(name_, capacity_)                                                      \
	uint8_t udcinput_buf_data_##name_[capacity_];                                              \
	struct udcinput_buf name_ = {                                                              \
		.data = udcinput_buf_data_##name_,                                                 \
		.size = 0,                                                                         \
		.capacity = capacity_,                                                             \
		.__buf = udcinput_buf_data_##name_,                                                \
	}

/** @brief Creates a new buffer and returns it. */
static inline struct udcinput_buf udcinput_buf_create(void *buffer, uint16_t capacity)
{
	return (struct udcinput_buf){
		.data = buffer,
		.size = 0,
		.capacity = capacity,
		.__buf = buffer,
	};
}

/** @brief Reset buffer data so it can be reused for other purposes. */
static inline void udcinput_buf_reset(struct udcinput_buf *buf)
{
	buf->size = 0;
	buf->data = buf->__buf;
}

/**
 * @brief Get the tail pointer for a buffer.
 *
 * Get a pointer to the end of the data in a buffer.
 *
 * @return Tail pointer for the buffer.
 */
static inline uint8_t *udcinput_buf_tail(struct udcinput_buf *buf)
{
	return buf->data + buf->size;
}

/**
 * @brief Check buffer headroom.
 *
 * Check how much free space there is in the beginning of the buffer.
 *
 * @return Number of bytes available in the beginning of the buffer.
 */
static inline size_t udcinput_buf_headroom(struct udcinput_buf *buf)
{
	return buf->data - buf->__buf;
}

/**
 * @brief Check buffer tailroom.
 *
 * Check how much free space there is at the end of the buffer.
 *
 * @return Number of bytes available at the end of the buffer.
 */
static inline size_t udcinput_buf_tailroom(struct udcinput_buf *buf)
{
	return buf->capacity - udcinput_buf_headroom(buf) - buf->size;
}

/**
 * @brief Resize the buffer
 *
 * If the new size is larger than the current one, the new bytes will be
 * initialized with @p value.
 *
 * @return 0 On success.
 * @return -ENOBUFS if @size is larger than the buffers capacity.
 */
static inline int udcinput_buf_resize(struct udcinput_buf *buf, size_t size, uint8_t value)
{
	if (size <= buf->size) {
		buf->size = size;
		return 0;
	}

	const size_t num_add = size - buf->size;
	if (udcinput_buf_tailroom(buf) < num_add) {
		return -ENOBUFS;
	}

	memset(udcinput_buf_tail(buf), value, num_add);
	buf->size = size;

	return 0;
}

/**
 * @brief Remove data from the beginning of the buffer.
 *
 * Removes data from the beginning of the buffer by modifying the data
 * pointer and buffer length.
 *
 * @param buf Buffer to update.
 * @param size Number of bytes to remove.
 *
 * @return New beginning of the buffer data.
 */
static inline void *udcinput_buf_pull(struct udcinput_buf *buf, size_t size)
{
	if (buf->size < size) {
		return NULL;
	}

	void *data = buf->data;
	buf->size -= size;
	buf->data += size;
	return data;
}

/**
 * @brief Remove data from the beginning of the buffer.
 *
 * Removes data from the beginning of the buffer by modifying the data
 * pointer and buffer length.
 *
 * @param buf Buffer to update.
 * @param size Number of bytes to remove.
 * @param out A pointer to the start of the memory will be stored here.
 *
 * @return 0 On success.
 * @return -ENOBUFS if the buffer is has less than @p size bytes available.
 */
static inline int udcinput_buf_pull_mem(struct udcinput_buf *buf, size_t size, void **out)
{
	*out = udcinput_buf_pull(buf, size);
	if (*out == NULL) {
		return -ENOBUFS;
	}
	return 0;
}

/**
 * @brief Remove a 8-bit value from the beginning of the buffer
 *
 * Same idea as with net_buf_pull(), but a helper for operating on
 * 8-bit values.
 *
 * @param buf A valid pointer on a buffer.
 * @param value The 8-bit removed value will be stored here.
 *
 * @return 0 On success.
 * @return -ENOBUFS if the buffer is has less than @p size bytes available.
 */
static inline int udcinput_buf_pull_u8(struct udcinput_buf *buf, uint8_t *value)
{
	uint8_t *data = udcinput_buf_pull(buf, sizeof(*value));
	if (data == NULL) {
		return -ENOBUFS;
	}

	*value = data[0];
	return 0;
}

/**
 * @brief Remove a 32-bit value from the beginning of the buffer
 *
 * Same idea as with udcinput_buf_pull(), but a helper for operating on
 * 32-bit little-endian data.
 *
 * @param buf A valid pointer on a buffer.
 * @param value The 32-bit removed value will be stored here.
 *
 * @return 0 On success.
 * @return -ENOBUFS if the buffer is has less than @p size bytes available.
 */
static inline int udcinput_buf_pull_u32le(struct udcinput_buf *buf, uint32_t *value)
{
	uint32_t *data = udcinput_buf_pull(buf, sizeof(*value));
	if (data == NULL) {
		return -ENOBUFS;
	}

	*value = le32toh(UNALIGNED_GET(data));
	return 0;
}

/**
 * @brief Prepare data to be added at the end of the buffer
 *
 * Increments the data length of a buffer to account for more data
 * at the end.
 *
 * @param buf Buffer to update.
 * @param size Number of bytes to increment the length with.
 *
 * @return The original tail of the buffer.
 */
static inline void *udcinput_buf_add(struct udcinput_buf *buf, size_t size)
{
	if (udcinput_buf_tailroom(buf) < size) {
		return NULL;
	}

	uint8_t *tail = udcinput_buf_tail(buf);
	buf->size += size;
	return tail;
}

/**
 * @brief Copy given number of bytes from memory to the end of the buffer
 *
 * Increments the data length of the  buffer to account for more data at the
 * end.
 *
 * @param buf Buffer to update.
 * @param src Location of data to be added.
 * @param size Length of data to be added
 *
 * @return 0 On success.
 * @return -ENOBUFS if the buffer is has less than @p size bytes available.
 */
static inline int udcinput_buf_add_mem(struct udcinput_buf *buf, const void *src, size_t size)
{
	uint8_t *data = udcinput_buf_add(buf, size);
	if (data == NULL) {
		return -ENOBUFS;
	}

	memcpy(data, src, size);
	return 0;
}

/**
 * @brief Add (8-bit) byte at the end of the buffer
 *
 * Increments the data length of the  buffer to account for more data at the
 * end.
 *
 * @param buf Buffer to update.
 * @param value byte value to be added.
 *
 * @return 0 On success.
 * @return -ENOBUFS if the buffer is has less than @p size bytes available.
 */
static inline int udcinput_buf_add_u8(struct udcinput_buf *buf, uint8_t value)
{
	uint8_t *data = udcinput_buf_add(buf, sizeof(value));
	if (data == NULL) {
		return -ENOBUFS;
	}

	*data = value;
	return 0;
}

/**
 * @brief Add 32-bit value at the end of the buffer
 *
 * Adds 32-bit value in little endian format at the end of buffer.
 * Increments the data length of a buffer to account for more data
 * at the end.
 *
 * @param buf Buffer to update.
 * @param value 32-bit value to be added.
 *
 * @return 0 On success.
 * @return -ENOBUFS if the buffer is has less than @p size bytes available.
 */
static inline int udcinput_buf_add_u32le(struct udcinput_buf *buf, uint32_t value)
{
	uint8_t *data = udcinput_buf_add(buf, sizeof(value));
	if (data == NULL) {
		return -ENOBUFS;
	}

	data[0] = value;
	data[1] = value >> 8;
	data[2] = value >> 16;
	data[3] = value >> 24;
	return 0;
}

/**
 * @brief Prints a format string to the remaining space of the buffer.
 *
 * A zero terminator will be written but not counted into the new size.
 *
 * @return 0 On success.
 * @return -ENOBUFS if the buffer is has less than @p size bytes available.
 */
__attribute__((format(printf, 2, 3))) static inline int
udcinput_buf_add_printf(struct udcinput_buf *buf, const char *fmt, ...)
{
	size_t tailroom = udcinput_buf_tailroom(buf);
	char *tail = (void *)udcinput_buf_tail(buf);

	va_list ap;
	va_start(ap, fmt);
	int ret = vsnprintf(tail, tailroom, fmt, ap);
	va_end(ap);

	if (ret < 0 || (size_t)ret >= tailroom) {
		return -ENOBUFS;
	}

	/* We don't include the null terminator, so you can concat more strings
	 * by repeatedly calling this function.
	 */
	buf->size += ret;
	return 0;
}

#endif /* UDCINPUT_BUF_P_H */
