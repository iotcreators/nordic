#
# Copyright (c) 2020 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iot_creators)

# Main application to build
target_sources(app PRIVATE src/main.c)

# adding subdirectories to the build
add_subdirectory(src/ui)

# adding all c header files (from target sources and add subdir)
include_directories(${PROJECT_SOURCE_DIR}/include)

zephyr_include_directories(src)
