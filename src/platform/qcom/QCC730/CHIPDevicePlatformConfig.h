/*
 *    Copyright (c) 2024 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 *    @file
 *          Platform-specific configuration overrides for the CHIP Device Layer
 *          on the Qualcomm QCC730MI platform (Zephyr RTOS, WiFi-only, no BLE).
 */

#pragma once

// Pull in the Zephyr base config first; we override specific values below.
#include <platform/Zephyr/CHIPDevicePlatformConfig.h>

// ---------------------------------------------------------------------------
// QCC730 does not have BLE hardware.
// ---------------------------------------------------------------------------
#undef  CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE
#define CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE 0

// ---------------------------------------------------------------------------
// QCC730 does not support Thread.
// ---------------------------------------------------------------------------
#undef  CHIP_DEVICE_CONFIG_ENABLE_THREAD
#define CHIP_DEVICE_CONFIG_ENABLE_THREAD 0

// ---------------------------------------------------------------------------
// WiFi is the only network interface on QCC730.
// ---------------------------------------------------------------------------
#ifndef CHIP_DEVICE_CONFIG_ENABLE_WIFI
#define CHIP_DEVICE_CONFIG_ENABLE_WIFI 1
#endif

// ---------------------------------------------------------------------------
// Event queue: increase from the Zephyr default (100) to avoid
// PostEventOrDie() abort() when the queue fills during WiFi connect.
// The Zephyr base config maps this to CONFIG_MAX_EVENT_QUEUE_SIZE which
// defaults to 100; we raise it here so the platform layer always has
// enough headroom even if the Kconfig value is not overridden.
// ---------------------------------------------------------------------------
#undef  CHIP_DEVICE_CONFIG_MAX_EVENT_QUEUE_SIZE
#define CHIP_DEVICE_CONFIG_MAX_EVENT_QUEUE_SIZE 200

// ---------------------------------------------------------------------------
// CHIP task stack size — QCC730 has limited RAM; keep at 8 kB.
// ---------------------------------------------------------------------------
#ifndef CHIP_DEVICE_CONFIG_CHIP_TASK_STACK_SIZE
#define CHIP_DEVICE_CONFIG_CHIP_TASK_STACK_SIZE (8 * 1024)
#endif
