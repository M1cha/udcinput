/*
 * Copyright (c) 2026 Michael Zimmermann
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef UDCINPUT_HID_H
#define UDCINPUT_HID_H

#include <udcinput.h>

/** @brief Locate and open the report interface of an HID function. */
int udcinput_hidg_open(const struct udcinput_function *function);

/** @brief Write an input report to @p f */
int udcinput_hidg_write_input_report(int fd, const struct udcinput_buf *buf);

#endif /* UDCINPUT_HID_H */
