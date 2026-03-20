/*
 * Copyright (c) 2026 Michael Zimmermann
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <udcinput.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <udcinput/buf_p.h>
#include <udcinput/hid.h>
#include <udcinput/util.h>

#define LOG_TAG "switchpro"
#include <udcinput/log_p.h>

/* NOTE: The negative side has 1 value more than the positive side. This maps
 * well to sunshine doing the same by using int16_t.
 */
#define STICK_MIN    (0)
#define STICK_CENTER ((UINT12_MAX >> 1) + 1)
#define STICK_MAX    (UINT12_MAX)

#define STICK_MAGNITUDE_MIN (STICK_CENTER - STICK_MIN)
#define STICK_MAGNITUDE_MAX (STICK_MAX - STICK_CENTER)

#define FLASH_SIZE 0x80000

#define OUTPUT_REPORT_ZERO              0x00
#define OUTPUT_REPORT_RUMBLE_AND_SUBCMD 0x01
#define OUTPUT_REPORT_RUMBLE            0x10
#define OUTPUT_REPORT_USB_CMD           0x80

#define USB_CMD_CONN_STATUS     0x01
#define USB_CMD_HANDSHAKE       0x02
#define USB_CMD_SET_BAUDRATE_3M 0x03
#define USB_CMD_TIMEOUT_OFF     0x04
#define USB_CMD_TIMEOUT_ON      0x05

#define SUBCMD_MANUAL_BT_PAIRING 0x01
#define SUBCMD_REQ_DEVINFO       0x02
#define SUBCMD_SET_REPORT_MODE   0x03
#define SUBCMD_TRIGGERS_ELAPSED  0x04
#define SUBCMD_LOW_POWER_MODE    0x08
#define SUBCMD_FLASH_READ        0x10
#define SUBCMD_SET_PLAYER_LEDS   0x30
#define SUBCMD_SET_HOME_LED      0x38
#define SUBCMD_ENABLE_IMU        0x40
#define SUBCMD_ENABLE_RUMBLE     0x48

#define RESPONSE_SUBCMD  0x21
#define RESPONSE_REPORT  0x30
#define RESPONSE_USB_CMD 0x81

#define CONTROLLER_TYPE_PRO 0x03

struct imu_cal {
	int16_t bias[3];
	int16_t sensitivity[3];
};

struct stick_cal {
	uint16_t min_x;
	uint16_t min_y;
	uint16_t center_x;
	uint16_t center_y;
	uint16_t max_x;
	uint16_t max_y;
};

struct color_config {
	uint8_t body[3];
	uint8_t buttons[3];
	uint8_t left_grip[3];
	uint8_t right_grip[3];
};

struct imu_horizontal_offsets {
	int16_t x;
	int16_t y;
	int16_t z;
};

/**
 * Parameters for one stick.
 *
 * The Switch maps the linear range deadzone_inner..deadzone_outer to the linear
 * range 0..32767 for each side separately (positive and negative).
 * This means, that games won't see sudden value jumps while crossing deadzones
 * and also can't see if the stick is in a deadzone.
 */
struct stick_params {
	uint16_t unknown0;
	uint16_t unknown1;

	/**
	 * The inner deadzone starts before `deadzone_inner` relative to the center.
	 *
	 * This acts on the raw value, so the actual percentage might be different
	 * for every stick and every axis.
	 */
	uint16_t deadzone_inner;

	/**
	 * The outer deadzone starts at `magnitude * (deadzone_outer/0xfff)`
	 * relative to the center.
	 *
	 * This makes it independent of the calibration, because it's effectively
	 * a percentage applied to whatever the current range is.
	 */
	uint16_t deadzone_outer;

	uint16_t unknown4;
	uint16_t unknown5;
	uint16_t unknown6;
	uint16_t unknown7;
	uint16_t unknown8;
	uint16_t unknown9;
	uint16_t unknown10;
	uint16_t unknown11;
};

static const uint8_t report_desc[] = {
	0x05, 0x01, 0x15, 0x00, 0x09, 0x04, 0xa1, 0x01, 0x85, 0x30, 0x05, 0x01, 0x05, 0x09, 0x19,
	0x01, 0x29, 0x0a, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x0a, 0x55, 0x00, 0x65, 0x00,
	0x81, 0x02, 0x05, 0x09, 0x19, 0x0b, 0x29, 0x0e, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95,
	0x04, 0x81, 0x02, 0x75, 0x01, 0x95, 0x02, 0x81, 0x03, 0x0b, 0x01, 0x00, 0x01, 0x00, 0xa1,
	0x00, 0x0b, 0x30, 0x00, 0x01, 0x00, 0x0b, 0x31, 0x00, 0x01, 0x00, 0x0b, 0x32, 0x00, 0x01,
	0x00, 0x0b, 0x35, 0x00, 0x01, 0x00, 0x15, 0x00, 0x27, 0xff, 0xff, 0x00, 0x00, 0x75, 0x10,
	0x95, 0x04, 0x81, 0x02, 0xc0, 0x0b, 0x39, 0x00, 0x01, 0x00, 0x15, 0x00, 0x25, 0x07, 0x35,
	0x00, 0x46, 0x3b, 0x01, 0x65, 0x14, 0x75, 0x04, 0x95, 0x01, 0x81, 0x02, 0x05, 0x09, 0x19,
	0x0f, 0x29, 0x12, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x04, 0x81, 0x02, 0x75, 0x08,
	0x95, 0x34, 0x81, 0x03, 0x06, 0x00, 0xff, 0x85, 0x21, 0x09, 0x01, 0x75, 0x08, 0x95, 0x3f,
	0x81, 0x03, 0x85, 0x81, 0x09, 0x02, 0x75, 0x08, 0x95, 0x3f, 0x81, 0x03, 0x85, 0x01, 0x09,
	0x03, 0x75, 0x08, 0x95, 0x3f, 0x91, 0x83, 0x85, 0x10, 0x09, 0x04, 0x75, 0x08, 0x95, 0x3f,
	0x91, 0x83, 0x85, 0x80, 0x09, 0x05, 0x75, 0x08, 0x95, 0x3f, 0x91, 0x83, 0x85, 0x82, 0x09,
	0x06, 0x75, 0x08, 0x95, 0x3f, 0x91, 0x83, 0xc0};

static void write_two_u12le(uint8_t data[3], uint16_t a, uint16_t b)
{
	data[0] = (a & 0xFF);
	data[1] = ((a >> 8) & 0xF) | ((b & 0xF) << 4);
	data[2] = ((b >> 4) & 0xFF);
}

static void write_u16le(uint8_t data[2], uint16_t a)
{
	data[0] = (a & 0xFF);
	data[1] = ((a >> 8) & 0xFF);
}

static void encode_axis_left_raw(uint8_t data[3], uint16_t x, uint16_t y)
{
	write_two_u12le(data, x, y);
}

static void encode_axis_right_raw(uint8_t data[3], uint16_t x, uint16_t y)
{
	write_two_u12le(data, x, y);
}

static uint16_t axis_moonlight_to_raw(int16_t value_)
{
	int32_t value = value_;

	if (value >= 0) {
		return STICK_CENTER + (value * STICK_MAGNITUDE_MAX / INT16_MAX);
	} else {
		return STICK_CENTER - (value * STICK_MAGNITUDE_MIN / INT16_MIN);
	}
}

static void encode_axis_left(uint8_t data[3], int16_t x, int16_t y)
{
	encode_axis_left_raw(data, axis_moonlight_to_raw(x), axis_moonlight_to_raw(y));
}

static void encode_axis_right(uint8_t data[3], int16_t x, int16_t y)
{
	encode_axis_right_raw(data, axis_moonlight_to_raw(x), axis_moonlight_to_raw(y));
}

static void encode_stick_cal_left(uint8_t data[9], const struct stick_cal *cal)
{
	write_two_u12le(&data[0], cal->max_x, cal->max_y);
	write_two_u12le(&data[3], cal->center_x, cal->center_y);
	write_two_u12le(&data[6], cal->min_x, cal->min_y);
}

static void encode_stick_cal_right(uint8_t data[9], const struct stick_cal *cal)
{
	write_two_u12le(&data[0], cal->center_x, cal->center_y);
	write_two_u12le(&data[3], cal->min_x, cal->min_y);
	write_two_u12le(&data[6], cal->max_x, cal->max_y);
}

static void encode_stick_params(uint8_t data[18], const struct stick_params *params)
{
	write_two_u12le(&data[0], params->unknown0, params->unknown1);
	write_two_u12le(&data[3], params->deadzone_inner, params->deadzone_outer);
	write_two_u12le(&data[6], params->unknown4, params->unknown5);
	write_two_u12le(&data[9], params->unknown6, params->unknown7);
	write_two_u12le(&data[12], params->unknown8, params->unknown9);
	write_two_u12le(&data[15], params->unknown10, params->unknown11);
}

static void encode_imu_cal(uint8_t data[12], const struct imu_cal *cal)
{
	write_u16le(&data[0], cal->bias[0]);
	write_u16le(&data[2], cal->bias[1]);
	write_u16le(&data[4], cal->bias[2]);

	write_u16le(&data[6], cal->sensitivity[0]);
	write_u16le(&data[8], cal->sensitivity[1]);
	write_u16le(&data[10], cal->sensitivity[2]);
}

static void encode_imu_horizontal_offsets(uint8_t data[6],
					  const struct imu_horizontal_offsets *offsets)
{
	write_u16le(&data[0], offsets->x);
	write_u16le(&data[2], offsets->y);
	write_u16le(&data[4], offsets->z);
}

static void encode_color_config(uint8_t data[12], const struct color_config *config)
{
	data[0] = config->body[0];
	data[1] = config->body[1];
	data[2] = config->body[2];

	data[3] = config->buttons[0];
	data[4] = config->buttons[1];
	data[5] = config->buttons[2];

	data[6] = config->left_grip[0];
	data[7] = config->left_grip[1];
	data[8] = config->left_grip[2];

	data[9] = config->right_grip[0];
	data[10] = config->right_grip[1];
	data[11] = config->right_grip[2];
}

int udcinput_gadget_configure_as_switchpro(struct udcinput_gadget *gadget)
{
	CHECK(udcinput_write_u16(gadget->path, "idVendor", 0x057e));
	CHECK(udcinput_write_u16(gadget->path, "idProduct", 0x2009));
	CHECK(udcinput_write_u16(gadget->path, "bcdDevice", 0x0201));
	CHECK(udcinput_write_u16(gadget->path, "bcdUSB", 0x0200));

	CHECK(udcinput_mkdir_fmt("%s/strings/0x409", gadget->path));
	CHECK(udcinput_write_string(gadget->path, "strings/0x409/serialnumber", "000000000001"));
	CHECK(udcinput_write_string(gadget->path, "strings/0x409/manufacturer",
				    "Nintendo Co., Ltd."));
	CHECK(udcinput_write_string(gadget->path, "strings/0x409/product", "Pro Controller"));

	CHECK(udcinput_mkdir_fmt("%s/configs/c.1", gadget->path));
	CHECK(udcinput_mkdir_fmt("%s/configs/c.1/strings/0x409", gadget->path));
	CHECK(udcinput_write_u16(gadget->path, "configs/c.1/MaxPower", 500));

	return 0;
}

static int encode_subcmd_response_header(struct udcinput_gamepad_switchpro *switchpro,
					 struct udcinput_buf *buf, uint8_t ack, uint8_t id)
{
	CHECK(udcinput_buf_add_u8(buf, RESPONSE_SUBCMD));

	/* Timer */
	CHECK(udcinput_buf_add_u8(buf, (switchpro->input_report[1])++));

	/* Input state */
	CHECK(udcinput_buf_add_mem(buf, &switchpro->input_report[2], 11));

	CHECK(udcinput_buf_add_u8(buf, ack));
	CHECK(udcinput_buf_add_u8(buf, id));

	return 0;
}

static int on_rumble(struct udcinput_gamepad_switchpro *switchpro, struct udcinput_buf *buf)
{
	uint32_t encoded_left;
	CHECKG(inbuf, udcinput_buf_pull_u32le(buf, &encoded_left));

	uint32_t encoded_right;
	CHECKG(inbuf, udcinput_buf_pull_u32le(buf, &encoded_right));

	LOG_DBG("on rumble: left=0x%08X right=0x%08X", encoded_left, encoded_right);

	return 0;

check_fail_inbuf:
	LOG_DBG("buffer is too small to contain valid rumble data");
	return -EINVAL;
}

static void on_rumble_and_subcmd(struct udcinput_gamepad_switchpro *switchpro, int fd,
				 struct udcinput_buf *buf)
{
	bool locked = false;

	uint8_t packet_num;
	CHECKG(inbuf, udcinput_buf_pull_u8(buf, &packet_num));

	if (on_rumble(switchpro, buf)) {
		return;
	}

	uint8_t id;
	CHECKG(inbuf, udcinput_buf_pull_u8(buf, &id));

	pthread_mutex_lock(&switchpro->mutex);
	locked = true;

	switch (id) {
	case SUBCMD_MANUAL_BT_PAIRING:
		LOG_DBG("manual bt pairing");

		udcinput_buf_reset(buf);
		CHECKG(outbuf, encode_subcmd_response_header(switchpro, buf, 0x81, id));

		/* Not sending valid data seems to prevent the Console from
		 * doing any further pairing attempts.
		 */

		CHECKG(outbuf, udcinput_buf_resize(buf, 64, 0));
		udcinput_hidg_write_input_report(fd, buf);
		break;

	case SUBCMD_REQ_DEVINFO:
		LOG_DBG("request devinfo");

		udcinput_buf_reset(buf);
		CHECKG(outbuf, encode_subcmd_response_header(switchpro, buf, 0x82, id));

		/* Firmware version */
		CHECKG(outbuf, udcinput_buf_add_u8(buf, 0x03));
		CHECKG(outbuf, udcinput_buf_add_u8(buf, 0x49));

		CHECKG(outbuf, udcinput_buf_add_u8(buf, CONTROLLER_TYPE_PRO));

		/* Unknown */
		CHECKG(outbuf, udcinput_buf_add_u8(buf, 0x02));

		/* Our Bluetooth MAC
		 *
		 * Use a different one for every controller so the Switch
		 * allows using them simultaneously
		 */
		CHECKG(outbuf, udcinput_buf_add_u8(buf, switchpro->function.id));
		CHECKG(outbuf, udcinput_buf_add_u8(buf, 0x00));
		CHECKG(outbuf, udcinput_buf_add_u8(buf, 0x00));
		CHECKG(outbuf, udcinput_buf_add_u8(buf, 0x00));
		CHECKG(outbuf, udcinput_buf_add_u8(buf, 0x00));
		CHECKG(outbuf, udcinput_buf_add_u8(buf, 0x00));

		/* Sensor type: I dumped this from my Pro controller. */
		CHECKG(outbuf, udcinput_buf_add_u8(buf, 0x03));

		/* Use colors from SPI flash: I dumped this from my Pro controller. */
		CHECKG(outbuf, udcinput_buf_add_u8(buf, 0x02));

		CHECKG(outbuf, udcinput_buf_resize(buf, 64, 0));
		udcinput_hidg_write_input_report(fd, buf);
		break;

	case SUBCMD_TRIGGERS_ELAPSED:
		LOG_DBG("triggers elapsed");

		udcinput_buf_reset(buf);
		CHECKG(outbuf, encode_subcmd_response_header(switchpro, buf, 0x83, id));

		/* Returning zeros for all values seems to work. */

		CHECKG(outbuf, udcinput_buf_resize(buf, 64, 0));
		udcinput_hidg_write_input_report(fd, buf);
		break;

	case SUBCMD_LOW_POWER_MODE: {
		uint8_t mode;
		CHECKG(inbuf, udcinput_buf_pull_u8(buf, &mode));
		LOG_DBG("Low Power Mode: 0x%02X", mode);

		/* We don't implement a low power mode, so there's nothing to do here. */

		udcinput_buf_reset(buf);
		CHECKG(outbuf, encode_subcmd_response_header(switchpro, buf, 0x80, id));

		CHECKG(outbuf, udcinput_buf_resize(buf, 64, 0));
		udcinput_hidg_write_input_report(fd, buf);
		break;
	}

	case SUBCMD_FLASH_READ: {
		uint32_t address;
		CHECKG(inbuf, udcinput_buf_pull_u32le(buf, &address));

		uint8_t size;
		CHECKG(inbuf, udcinput_buf_pull_u8(buf, &size));

		uint32_t end = address + size;
		if (end < address || end > FLASH_SIZE) {
			pthread_mutex_unlock(&switchpro->mutex);
			LOG_ERR("Out of bounds flash read at 0x%08X+0x%x", address, size);
			return;
		}

		udcinput_buf_reset(buf);
		CHECKG(outbuf, encode_subcmd_response_header(switchpro, buf, 0x90, id));

		CHECKG(outbuf, udcinput_buf_add_u32le(buf, address));
		CHECKG(outbuf, udcinput_buf_add_u8(buf, size));
		CHECKG(outbuf, udcinput_buf_add_mem(buf, switchpro->flash_data + address, size));

		CHECKG(outbuf, udcinput_buf_resize(buf, 64, 0));
		udcinput_hidg_write_input_report(fd, buf);
		break;
	}

	case SUBCMD_ENABLE_IMU: {
		uint8_t enable;
		CHECKG(inbuf, udcinput_buf_pull_u8(buf, &enable));
		LOG_DBG("Enable IMU: 0x%02X", enable);

		/* We don't support motion controls, yet. */

		udcinput_buf_reset(buf);
		CHECKG(outbuf, encode_subcmd_response_header(switchpro, buf, 0x80, id));

		CHECKG(outbuf, udcinput_buf_resize(buf, 64, 0));
		udcinput_hidg_write_input_report(fd, buf);
		break;
	}

	case SUBCMD_SET_REPORT_MODE: {
		uint8_t report_mode;
		CHECKG(inbuf, udcinput_buf_pull_u8(buf, &report_mode));
		LOG_DBG("Report Mode: 0x%02X", report_mode);

		/* We ignore this and always report in the same mode. */

		udcinput_buf_reset(buf);
		CHECKG(outbuf, encode_subcmd_response_header(switchpro, buf, 0x80, id));

		CHECKG(outbuf, udcinput_buf_resize(buf, 64, 0));
		udcinput_hidg_write_input_report(fd, buf);
		break;
	}

	case SUBCMD_ENABLE_RUMBLE: {
		uint8_t enable;
		CHECKG(inbuf, udcinput_buf_pull_u8(buf, &enable));
		LOG_DBG("Enable Rumble: 0x%02X", enable);

		/* We don't support rumble, yet. */

		udcinput_buf_reset(buf);
		CHECKG(outbuf, encode_subcmd_response_header(switchpro, buf, 0x80, id));

		CHECKG(outbuf, udcinput_buf_resize(buf, 64, 0));
		udcinput_hidg_write_input_report(fd, buf);
		break;
	}

	case SUBCMD_SET_PLAYER_LEDS: {
		uint8_t config;
		CHECKG(inbuf, udcinput_buf_pull_u8(buf, &config));
		LOG_DBG("Player LEDs: 0x%02X", config);

		/* Sunshine doesn't support player LEDs. */

		udcinput_buf_reset(buf);
		CHECKG(outbuf, encode_subcmd_response_header(switchpro, buf, 0x80, id));

		CHECKG(outbuf, udcinput_buf_resize(buf, 64, 0));
		udcinput_hidg_write_input_report(fd, buf);
		break;
	}

	case SUBCMD_SET_HOME_LED:
		LOG_DBG("Set Home LED");

		/* Sunshine doesn't support the home LED. */

		udcinput_buf_reset(buf);
		CHECKG(outbuf, encode_subcmd_response_header(switchpro, buf, 0x80, id));

		CHECKG(outbuf, udcinput_buf_resize(buf, 64, 0));
		udcinput_hidg_write_input_report(fd, buf);
		break;

	default:
		LOG_DBG("Unsupported subcmd id: 0x%02X", id);
		LOG_HEXDUMP_DBG(buf->data, buf->size);

		udcinput_buf_reset(buf);
		CHECKG(outbuf, encode_subcmd_response_header(switchpro, buf, 0x80, id));
		CHECKG(outbuf, udcinput_buf_add_u8(buf, 0x03));

		CHECKG(outbuf, udcinput_buf_resize(buf, 64, 0));
		udcinput_hidg_write_input_report(fd, buf);
		break;
	}

	pthread_mutex_unlock(&switchpro->mutex);
	return;

check_fail_inbuf:
	if (locked) {
		pthread_mutex_unlock(&switchpro->mutex);
	}
	LOG_DBG("buffer is too small to be a valid subcmd output report");
	return;
check_fail_outbuf:
	if (locked) {
		pthread_mutex_unlock(&switchpro->mutex);
	}
	LOG_DBG("buffer is too small to write a subcmd response");
	return;
}

static void on_usb_cmd(struct udcinput_gamepad_switchpro *switchpro, int fd,
		       struct udcinput_buf *buf)
{
	uint8_t id;
	if (udcinput_buf_pull_u8(buf, &id)) {
		LOG_DBG("usb cmd buffer is too small");
		return;
	}

	switch (id) {
	case USB_CMD_CONN_STATUS:
		LOG_DBG("conn status");
		udcinput_buf_reset(buf);
		CHECKG(outbuf, udcinput_buf_add_u8(buf, RESPONSE_USB_CMD));
		CHECKG(outbuf, udcinput_buf_add_u8(buf, id));

		/* Connection Status: 0 seems to be the only one that works. */
		CHECKG(outbuf, udcinput_buf_add_u8(buf, 0x00));

		CHECKG(outbuf, udcinput_buf_add_u8(buf, CONTROLLER_TYPE_PRO));

		/* Our Reversed Bluetooth MAC
		 *
		 * Use a different one for every controller so the Switch
		 * allows using them simultaneously
		 */
		CHECKG(outbuf, udcinput_buf_add_u8(buf, 0x00));
		CHECKG(outbuf, udcinput_buf_add_u8(buf, 0x00));
		CHECKG(outbuf, udcinput_buf_add_u8(buf, 0x00));
		CHECKG(outbuf, udcinput_buf_add_u8(buf, 0x00));
		CHECKG(outbuf, udcinput_buf_add_u8(buf, 0x00));
		CHECKG(outbuf, udcinput_buf_add_u8(buf, switchpro->function.id));

		CHECKG(outbuf, udcinput_buf_resize(buf, 64, 0));
		udcinput_hidg_write_input_report(fd, buf);
		break;
	case USB_CMD_HANDSHAKE:
		LOG_DBG("handshake");
		udcinput_buf_reset(buf);
		CHECKG(outbuf, udcinput_buf_add_u8(buf, RESPONSE_USB_CMD));
		CHECKG(outbuf, udcinput_buf_add_u8(buf, id));
		CHECKG(outbuf, udcinput_buf_resize(buf, 64, 0));
		udcinput_hidg_write_input_report(fd, buf);
		break;
	case USB_CMD_SET_BAUDRATE_3M:
		LOG_DBG("baudrate-3m");

		/* We don't use UART, so there's nothing to do here. */

		udcinput_buf_reset(buf);
		CHECKG(outbuf, udcinput_buf_add_u8(buf, RESPONSE_USB_CMD));
		CHECKG(outbuf, udcinput_buf_add_u8(buf, id));
		udcinput_hidg_write_input_report(fd, buf);
		break;
	case USB_CMD_TIMEOUT_OFF:
		LOG_DBG("timeout-off");

		/* We don't implement the timeout. */

		/* We don't have to send a response. */
		break;
	case USB_CMD_TIMEOUT_ON:
		LOG_DBG("timeout-on");

		/* We don't implement the timeout. */

		/* We don't have to send a response. */
		break;
	default:
		LOG_DBG("Unsupported usb cmd id: 0x%02X", id);
		LOG_HEXDUMP_DBG(buf->data, buf->size);
		break;
	}
	return;

check_fail_outbuf:
	LOG_DBG("buffer is too small to write a usb cmd response");
	return;
}

static void on_output_report(void *const switchpro_, int fd, struct udcinput_buf *buf)
{
	struct udcinput_gamepad_switchpro *const switchpro = switchpro_;

	uint8_t id;
	if (udcinput_buf_pull_u8(buf, &id)) {
		LOG_DBG("output report buffer is too small");
		return;
	}

	switch (id) {
	case OUTPUT_REPORT_ZERO:
		/* I don't know why this happens. The Switch doesn't do that
		 * for an original controller. Since the first 3 commands are
		 * this, it's probably caused by one of the differences in our
		 * USB device descriptor.
		 *
		 * When I send this to a Pro controller it doesn't respond so
		 * let's do the same here.
		 */
		LOG_DBG("zero");
		LOG_HEXDUMP_DBG(buf->data, buf->size);
		break;
	case OUTPUT_REPORT_RUMBLE_AND_SUBCMD:
		on_rumble_and_subcmd(switchpro, fd, buf);
		break;
	case OUTPUT_REPORT_RUMBLE: {
		uint8_t timer;
		if (udcinput_buf_pull_u8(buf, &timer)) {
			LOG_DBG("rumble-only packet is too short");
			return;
		}

		on_rumble(switchpro, buf);
		break;
	}
	case OUTPUT_REPORT_USB_CMD:
		on_usb_cmd(switchpro, fd, buf);
		break;
	default:
		LOG_DBG("Unsupported output report id: 0x%02X", id);
		LOG_HEXDUMP_DBG(buf->data, buf->size);
		break;
	}
}

static int flash_write(struct udcinput_gamepad_switchpro *switchpro, uint32_t offset,
		       const void *data, size_t size)
{
	uint32_t end = offset + size;
	if (size > UINT32_MAX || end < offset || end > FLASH_SIZE) {
		LOG_ERR("Out of bounds flash write at 0x%08X+0x%zx", offset, size);
		return -EINVAL;
	}

	memcpy(switchpro->flash_data + offset, data, size);
	return 0;
}

static int init(struct udcinput_gamepad_switchpro *switchpro)
{
	const char *path = switchpro->function.path;

	switchpro->input_report[0] = RESPONSE_REPORT;

	/* Charging, USB-Powered */
	switchpro->input_report[2] = 0x91;

	/* Vibrator */
	switchpro->input_report[12] = 0x00;

	encode_axis_left(&switchpro->input_report[6], 0, 0);
	encode_axis_right(&switchpro->input_report[9], 0, 0);

	static const struct imu_cal accel_cal = {
		.bias = {0, 0, 0},
		.sensitivity = {16384, 16384, 16384},
	};
	uint8_t imu_cal_raw[12];
	encode_imu_cal(imu_cal_raw, &accel_cal);
	CHECK(flash_write(switchpro, 0x6020, imu_cal_raw, sizeof(imu_cal_raw)));

	static const struct imu_cal gyro_cal = {
		.bias = {0, 0, 0},
		.sensitivity = {13371, 13371, 13371},
	};
	encode_imu_cal(imu_cal_raw, &gyro_cal);
	CHECK(flash_write(switchpro, 0x602C, imu_cal_raw, sizeof(imu_cal_raw)));

	static const struct stick_cal stick_cal = {
		.min_x = STICK_MAGNITUDE_MIN,
		.min_y = STICK_MAGNITUDE_MIN,
		.center_x = STICK_CENTER,
		.center_y = STICK_CENTER,
		.max_x = STICK_MAGNITUDE_MAX,
		.max_y = STICK_MAGNITUDE_MAX,
	};
	uint8_t stick_cal_raw[9];

	encode_stick_cal_left(stick_cal_raw, &stick_cal);
	CHECK(flash_write(switchpro, 0x603D, stick_cal_raw, sizeof(stick_cal_raw)));

	encode_stick_cal_right(stick_cal_raw, &stick_cal);
	CHECK(flash_write(switchpro, 0x6046, stick_cal_raw, sizeof(stick_cal_raw)));

	static const struct color_config color_config = {
		.body = {0x00, 0x00, 0x00},
		.buttons = {0xFF, 0xFF, 0xFF},
		.left_grip = {0xFE, 0x9B, 0x00},
		.right_grip = {0xFE, 0x9B, 0x00},
	};
	uint8_t color_config_raw[12];

	encode_color_config(color_config_raw, &color_config);
	CHECK(flash_write(switchpro, 0x6050, color_config_raw, sizeof(color_config_raw)));

	static const struct imu_horizontal_offsets imu_horizontal_offsets = {
		.x = 0,
		.y = 0,
		.z = 0,
	};
	uint8_t imu_horizontal_offsets_raw[6];
	encode_imu_horizontal_offsets(imu_horizontal_offsets_raw, &imu_horizontal_offsets);
	CHECK(flash_write(switchpro, 0x6080, imu_horizontal_offsets_raw,
			  sizeof(imu_horizontal_offsets_raw)));

	const uint16_t max_magnitude = MAX(STICK_MAGNITUDE_MIN, STICK_MAGNITUDE_MAX);
	const float deadzone_inner_percent = 0.10;
	const float deadzone_outer_percent = 0.95;

	/* The unknown values were dumped from a Pro controller. */
	static const struct stick_params stick_params = {
		.unknown0 = 0x00F,
		.unknown1 = 0x613,
		.deadzone_inner = max_magnitude * deadzone_inner_percent,
		.deadzone_outer = 0xFFF * deadzone_outer_percent,
		.unknown4 = 0x4D4,
		.unknown5 = 0x541,
		.unknown6 = 0x541,
		.unknown7 = 0x541,
		.unknown8 = 0x9C7,
		.unknown9 = 0x9C7,
		.unknown10 = 0x633,
		.unknown11 = 0x633,
	};
	uint8_t stick_params_raw[18];

	encode_stick_params(stick_params_raw, &stick_params);
	CHECK(flash_write(switchpro, 0x6086, stick_params_raw, sizeof(stick_params_raw)));

	/* Use the smallest possible interval no matter the speed. The Switch seems to limit this
	 * to 62.5Hz anyway. */
	CHECK(udcinput_write_u16(path, "interval", 1));
	CHECK(udcinput_write_u16(path, "protocol", 0));
	CHECK(udcinput_write_u16(path, "report_length", 64));
	CHECK(udcinput_write_buf(path, "report_desc", report_desc, sizeof(report_desc)));
	CHECK(udcinput_function_enable(&switchpro->function, "c.1"));

	return 0;
}

int udcinput_gamepad_switchpro_create(struct udcinput_gamepad_switchpro *switchpro,
				      struct udcinput_gadget *gadget)
{
	void *flash_data = calloc(FLASH_SIZE, 1);
	if (flash_data == NULL) {
		return -ENOMEM;
	}

	/* Flash chips use 0xFF as their erase value. */
	memset(flash_data, 0xFF, FLASH_SIZE);

	*switchpro = (struct udcinput_gamepad_switchpro){
		.flash_data = flash_data,
		.fd = -1,
	};

	int ret = udcinput_function_create(&switchpro->function, gadget, "hid");
	if (ret < 0) {
		free(flash_data);
		return ret;
	}

	pthread_mutex_init(&switchpro->mutex, NULL);

	ret = init(switchpro);
	if (ret < 0) {
		udcinput_function_destroy(&switchpro->function);
		free(flash_data);
		return ret;
	}

	return 0;
}

void udcinput_gamepad_switchpro_destroy(struct udcinput_gamepad_switchpro *switchpro)
{
	pthread_mutex_destroy(&switchpro->mutex);
	udcinput_function_destroy(&switchpro->function);
	free(switchpro->flash_data);

	if (switchpro->fd >= 0) {
		close(switchpro->fd);
	}
}

void udcinput_gamepad_switchpro_set_state(struct udcinput_gamepad_switchpro *switchpro,
					  const struct udcinput_gamepad_state *state)
{
	uint8_t buffer[9] = {0};

	if (state->buttons & UDCINPUT_BUTTON_Y) {
		buffer[0] |= BIT(0);
	}
	if (state->buttons & UDCINPUT_BUTTON_X) {
		buffer[0] |= BIT(1);
	}
	if (state->buttons & UDCINPUT_BUTTON_B) {
		buffer[0] |= BIT(2);
	}
	if (state->buttons & UDCINPUT_BUTTON_A) {
		buffer[0] |= BIT(3);
	}
	if (state->buttons & UDCINPUT_BUTTON_RIGHT) {
		buffer[0] |= BIT(6);
	}
	if (state->trigger_right > 0) {
		buffer[0] |= BIT(7);
	}

	if (state->buttons & UDCINPUT_BUTTON_BACK) {
		/* Minus */
		buffer[1] |= BIT(0);
	}
	if (state->buttons & UDCINPUT_BUTTON_START) {
		/* Plus */
		buffer[1] |= BIT(1);
	}
	if (state->buttons & UDCINPUT_BUTTON_RIGHT_STICK) {
		buffer[1] |= BIT(2);
	}
	if (state->buttons & UDCINPUT_BUTTON_LEFT_STICK) {
		buffer[1] |= BIT(3);
	}
	if (state->buttons & UDCINPUT_BUTTON_HOME) {
		buffer[1] |= BIT(4);
	}
	if (state->buttons & UDCINPUT_BUTTON_MISC) {
		/* Capture */
		buffer[1] |= BIT(5);
	}
	/* Charging Grip. The Pro controller always has this set. */
	buffer[1] |= BIT(8);

	if (state->buttons & UDCINPUT_BUTTON_DPAD_DOWN) {
		buffer[2] |= BIT(0);
	}
	if (state->buttons & UDCINPUT_BUTTON_DPAD_UP) {
		buffer[2] |= BIT(1);
	}
	if (state->buttons & UDCINPUT_BUTTON_DPAD_RIGHT) {
		buffer[2] |= BIT(2);
	}
	if (state->buttons & UDCINPUT_BUTTON_DPAD_LEFT) {
		buffer[2] |= BIT(3);
	}
	if (state->buttons & UDCINPUT_BUTTON_LEFT) {
		buffer[2] |= BIT(6);
	}
	if (state->trigger_left > 0) {
		buffer[2] |= BIT(7);
	}

	encode_axis_left(&buffer[3], state->stick_left.x, state->stick_left.y);
	encode_axis_right(&buffer[6], state->stick_right.x, state->stick_right.y);

	struct udcinput_buf buf =
		udcinput_buf_create(switchpro->input_report, sizeof(switchpro->input_report));
	buf.size = buf.capacity;

	pthread_mutex_lock(&switchpro->mutex);

	if (switchpro->fd < 0) {
		pthread_mutex_unlock(&switchpro->mutex);
		return;
	}

	/* Timer */
	switchpro->input_report[1] += 1;

	memcpy(&switchpro->input_report[3], buffer, sizeof(buffer));

	udcinput_hidg_write_input_report(switchpro->fd, &buf);

	pthread_mutex_unlock(&switchpro->mutex);
}

static int switchpro_open(void *const switchpro_)
{
	struct udcinput_gamepad_switchpro *const switchpro = switchpro_;

	pthread_mutex_lock(&switchpro->mutex);
	if (switchpro->fd < 0) {
		switchpro->fd = udcinput_hidg_open(&switchpro->function);
	}
	pthread_mutex_unlock(&switchpro->mutex);

	return switchpro->fd;
}

static void switchpro_close(void *const switchpro_)
{
	struct udcinput_gamepad_switchpro *const switchpro = switchpro_;

	pthread_mutex_lock(&switchpro->mutex);
	if (switchpro->fd >= 0) {
		close(switchpro->fd);
		switchpro->fd = -1;
	}
	pthread_mutex_unlock(&switchpro->mutex);
}

struct udcinput_loop_callbacks udcinput_gamepad_switchpro_loop_callbacks = {
	.open = switchpro_open,
	.close = switchpro_close,
	.on_data = on_output_report,
};
