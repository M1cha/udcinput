# Copyright (c) 2026 Michael Zimmermann
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(udcinput)

set(UDCINPUT_LOG_LEVEL 4 CACHE STRING "log level for the udcinput library")

add_library(udcinput
	src/hid.c
	src/log.c
	src/util.c
	src/udcinput.c
	src/gamepads/switchpro.c
)
target_include_directories(udcinput PUBLIC
	include
)
target_include_directories(udcinput PRIVATE
	include-private
)
target_compile_options(udcinput PRIVATE
	-Wall
	-Wextra
	-Wshadow
	-Wmissing-prototypes
	-Wno-unused-parameter
	-Werror
)
target_compile_definitions(udcinput PRIVATE
	-DUDCINPUT_LOG_LEVEL=${UDCINPUT_LOG_LEVEL}
)
