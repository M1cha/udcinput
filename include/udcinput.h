/*
 * Copyright (c) 2026 Michael Zimmermann
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef UDCINPUT_H
#define UDCINPUT_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <udcinput/buf.h>

struct udcinput_gadget {
	char *path;
	size_t next_function_id;
	bool enabled;
};

void udcinput_gadget_create(struct udcinput_gadget *gadget);
int udcinput_gadget_init(struct udcinput_gadget *gadget, const char *configdir, const char *name,
			 bool cleanup);
void udcinput_gadget_destroy(struct udcinput_gadget *gadget);
int udcinput_gadget_enable(struct udcinput_gadget *gadget, const char *udc_name);
int udcinput_gadget_disable(struct udcinput_gadget *gadget);

struct udcinput_function {
	char *path;
	char *symlink_path;
	size_t id;
};

int udcinput_function_create(struct udcinput_function *function, struct udcinput_gadget *gadget,
			     const char *name);
void udcinput_function_destroy(struct udcinput_function *function);
int udcinput_function_enable(struct udcinput_function *function, const char *config);

struct udcinput_loop_callbacks {
	int (*open)(void *user_data);
	void (*close)(void *user_data);
	void (*on_data)(void *user_data, int fd, struct udcinput_buf *);
};

struct udcinput_loop {
	int epollfd;
	int stopfd;
	int datafd;
};

int udcinput_loop_create(struct udcinput_loop *loop);
void udcinput_loop_destroy(struct udcinput_loop *loop);
int udcinput_loop_run(struct udcinput_loop *loop, void *user_data,
		      const struct udcinput_loop_callbacks *callbacks);
int udcinput_loop_stop(struct udcinput_loop *loop);

#define UDCINPUT_BUTTON_DPAD_UP     0x0001
#define UDCINPUT_BUTTON_DPAD_DOWN   0x0002
#define UDCINPUT_BUTTON_DPAD_LEFT   0x0004
#define UDCINPUT_BUTTON_DPAD_RIGHT  0x0008
#define UDCINPUT_BUTTON_START       0x0010
#define UDCINPUT_BUTTON_BACK        0x0020
#define UDCINPUT_BUTTON_LEFT_STICK  0x0040
#define UDCINPUT_BUTTON_RIGHT_STICK 0x0080
#define UDCINPUT_BUTTON_LEFT        0x0100
#define UDCINPUT_BUTTON_RIGHT       0x0200
#define UDCINPUT_BUTTON_HOME        0x0400
#define UDCINPUT_BUTTON_A           0x1000
#define UDCINPUT_BUTTON_B           0x2000
#define UDCINPUT_BUTTON_X           0x4000
#define UDCINPUT_BUTTON_Y           0x8000
#define UDCINPUT_BUTTON_PADDLE1     0x010000
#define UDCINPUT_BUTTON_PADDLE2     0x020000
#define UDCINPUT_BUTTON_PADDLE3     0x040000
#define UDCINPUT_BUTTON_PADDLE4     0x080000
#define UDCINPUT_BUTTON_TOUCHPAD    0x100000
#define UDCINPUT_BUTTON_MISC        0x200000

struct udcinput_gamepad_stick {
	int16_t x;
	int16_t y;
};

struct udcinput_gamepad_state {
	uint32_t buttons;
	uint8_t trigger_left;
	uint8_t trigger_right;
	struct udcinput_gamepad_stick stick_left;
	struct udcinput_gamepad_stick stick_right;
};

struct udcinput_gamepad_callbacks {
	void (*on_rumble)(void *user_data, uint16_t low, uint16_t high);
};

struct udcinput_gamepad_switchpro {
	struct udcinput_function function;
	uint8_t *flash_data;

	pthread_mutex_t mutex;
	int fd;
	int fd_nonblock;
	uint8_t input_report[64];

	const struct udcinput_gamepad_callbacks *callbacks;
	void *user_data;
};

extern struct udcinput_loop_callbacks udcinput_gamepad_switchpro_loop_callbacks;

int udcinput_gadget_configure_as_switchpro(struct udcinput_gadget *gadget);
int udcinput_gamepad_switchpro_create(struct udcinput_gamepad_switchpro *switchpro,
				      struct udcinput_gadget *gadget);
void udcinput_gamepad_switchpro_destroy(struct udcinput_gamepad_switchpro *switchpro);
bool udcinput_gamepad_switchpro_set_state(struct udcinput_gamepad_switchpro *switchpro,
					  const struct udcinput_gamepad_state *state);

#endif /* UDCINPUT_H */
