/*
 *    Copyright (c) 2024 Project CHIP Authors
 *    All rights reserved.
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

#pragma once

/**
 * @file
 *   Project-specific configuration overrides for QCC730 Lock App.
 */

#pragma once

// Disable thread_local in ErrorStr.cpp to avoid __aeabi_read_tp linker error
#define CHIP_SYSTEM_CONFIG_THREAD_LOCAL_STORAGE 0

// Increase event queue size to prevent abort() during high traffic (e.g. WiFi connect)
#define CHIP_DEVICE_CONFIG_MAX_EVENT_QUEUE_SIZE 200

// Increase MRP Session Active Interval (SAI) advertised to peers.
// Required for SPAKE2+ to complete on QCC730 (Cortex-M33 @80MHz).
// See all-clusters-app CHIPProjectConfig.h for full explanation.
#define CHIP_CONFIG_MRP_LOCAL_ACTIVE_RETRY_INTERVAL (5000_ms32)

// Increase MRP Idle Retry Interval (II) for CASE Sigma1 handling.
#define CHIP_CONFIG_MRP_LOCAL_IDLE_RETRY_INTERVAL (5000_ms32)

// Default pool size is 3 (ReportScheduler + DnssdServer + ICDManagementServer).
// AppTask also registers as an ICDStateObserver, so we need 4.
#define CHIP_CONFIG_ICD_OBSERVERS_POOL_SIZE 4
