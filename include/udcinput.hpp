/*
 * Copyright (c) 2026 Michael Zimmermann
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

extern "C" {
#include <udcinput.h>
}

#include <optional>
#include <utility>

namespace udcinput
{

/* NOTE: This works because all of our C structs are copyable, since they don't
 *       contain pointers to each other.
 */
template <typename T, auto DestroyFn> class CStructWrapper
{
      protected:
	CStructWrapper() = default;

	explicit CStructWrapper(T value) : raw(value), valid(true)
	{
	}

      public:
	T raw{};
	bool valid = false;

	~CStructWrapper()
	{
		if (valid) {
			DestroyFn(&raw);
		}
	}

	CStructWrapper(const CStructWrapper &) = delete;
	CStructWrapper &operator=(const CStructWrapper &) = delete;

	CStructWrapper(CStructWrapper &&other)
	{
		raw = std::move(other.raw);
		std::swap(valid, other.valid);
	}

	CStructWrapper &operator=(CStructWrapper &&) = delete;
};

template <typename F1> struct GamepadCallbacks {
      public:
	F1 on_rumble;

	const struct udcinput_gamepad_callbacks raw = {
		.on_rumble = c_on_rumble,
	};

	static void delete_userdata(void *user_data)
	{
		auto *self = static_cast<GamepadCallbacks *>(user_data);
		delete self;
	}

      private:
	static void c_on_rumble(void *user_data, uint16_t low, uint16_t high)
	{
		auto *self = static_cast<GamepadCallbacks *>(user_data);
		self->on_rumble(low, high);
	}
};

struct Gadget: CStructWrapper<struct udcinput_gadget, udcinput_gadget_destroy> {
	Gadget()
	{
		udcinput_gadget_create(&raw);
		valid = true;
	}

	int init(const char *configdir, const char *name, bool cleanup)
	{
		return udcinput_gadget_init(&raw, configdir, name, cleanup);
	}

	int configure_as_switchpro()
	{
		return udcinput_gadget_configure_as_switchpro(&raw);
	}

	int enable(const char *udc_name)
	{
		return udcinput_gadget_enable(&raw, udc_name);
	}

	int disable()
	{
		return udcinput_gadget_disable(&raw);
	}
};

struct SwitchPro
	: CStructWrapper<struct udcinput_gamepad_switchpro, udcinput_gamepad_switchpro_destroy> {
	using CStructWrapper::CStructWrapper;
	void (*delete_userdata)(void *user_data) = nullptr;

	SwitchPro() = delete;

	~SwitchPro()
	{
		if (delete_userdata) {
			delete_userdata(raw.user_data);
		}
	}

	SwitchPro(SwitchPro &&other) : CStructWrapper(std::move(other))
	{
		std::swap(delete_userdata, other.delete_userdata);
	}

	static std::optional<SwitchPro> create(Gadget &gadget)
	{
		struct udcinput_gamepad_switchpro switchpro;
		if (udcinput_gamepad_switchpro_create(&switchpro, &gadget.raw) < 0) {
			return std::nullopt;
		}
		return SwitchPro(switchpro);
	}

	void set_state(const struct udcinput_gamepad_state &state)
	{
		udcinput_gamepad_switchpro_set_state(&raw, &state);
	}

	struct udcinput_loop_callbacks &loop_callbacks()
	{
		return udcinput_gamepad_switchpro_loop_callbacks;
	}

	template <typename F1> void set_callbacks(GamepadCallbacks<F1> callbacks)
	{
		if (delete_userdata) {
			delete_userdata(raw.user_data);
		}

		auto callbacks_heap = new GamepadCallbacks(callbacks);
		raw.user_data = callbacks_heap;
		raw.callbacks = &callbacks_heap->raw;
	}
};

struct Loop: CStructWrapper<struct udcinput_loop, udcinput_loop_destroy> {
	using CStructWrapper::CStructWrapper;
	Loop() = delete;

	static std::optional<Loop> create()
	{
		struct udcinput_loop loop;
		if (udcinput_loop_create(&loop) < 0) {
			return std::nullopt;
		}
		return Loop(loop);
	}

	int run(void *user_data, const udcinput_loop_callbacks &callbacks)
	{
		return udcinput_loop_run(&raw, user_data, &callbacks);
	}

	int stop()
	{
		return udcinput_loop_stop(&raw);
	}
};

} /* namespace udcinput */
