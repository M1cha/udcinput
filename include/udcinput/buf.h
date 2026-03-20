/*
 * Copyright (c) 2026 Michael Zimmermann
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef UDCINPUT_BUF_H
#define UDCINPUT_BUF_H

/**
 * @brief A buffer for encoding and decoding data sequentially.
 */
struct udcinput_buf {
	/** Pointer to the start of data in the buffer. */
	uint8_t *data;

	/** Length of the data behind the data pointer. */
	uint16_t size;

	/** Amount of data that __buf can store. */
	uint16_t capacity;

	/** Start of the data storage. */
	uint8_t *__buf;
};

#endif /* UDCINPUT_BUF_H */
