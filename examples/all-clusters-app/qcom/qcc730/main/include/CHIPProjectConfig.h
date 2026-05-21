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

/**
 * @file
 *   Example project configuration file for CHIP.
 *
 *   This is a place to put application or project-specific overrides
 *   to the default configuration values for general CHIP features.
 */

#pragma once

// All clusters app has 3 group endpoints.
#define CHIP_CONFIG_MAX_GROUP_ENDPOINTS_PER_FABRIC 3

#define CHIP_CONFIG_ENABLE_ACL_EXTENSIONS 1

// Disable thread_local in ErrorStr.cpp to avoid __aeabi_read_tp linker error
#define CHIP_SYSTEM_CONFIG_THREAD_LOCAL_STORAGE 0

// Increase event queue size to prevent abort() during high traffic (e.g. WiFi connect)
#define CHIP_DEVICE_CONFIG_MAX_EVENT_QUEUE_SIZE 200

// Increase MRP Session Active Interval (SAI) advertised to peers.
//
// Root cause: SPAKE2+ HandlePake1() performs ~5 P-256 scalar multiplications
// synchronously on the CHIP thread, taking ~4-6s on QCC730 (Cortex-M33 @80MHz)
// even after ECC optimization (ECP_WINDOW=4, ECP_FIXED_POINT_OPTIM=1).
//
// The default SAI=300ms causes chip-tool to calculate a MRP retransmit timeout
// of only ~3.6-4.0s (4 retries with exponential backoff):
//   i = 300ms x 1.1 = 330ms
//   Retries: 330 + 330 + 528 + 845 + 1352 = ~3387ms total
// chip-tool gives up on Pake1 before QCC730 finishes computing Pake2.
//
// Setting SAI=5000ms makes chip-tool's first retry interval 5500ms,
// giving QCC730 sufficient time to complete ECC computation (~4-6s).
// This value is advertised in mDNS TXT (SAI key) and in PBKDFParamResponse.
#define CHIP_CONFIG_MRP_LOCAL_ACTIVE_RETRY_INTERVAL (5000_ms32)

// Increase MRP Idle Retry Interval (II) advertised to peers.
//
// Root cause: CASE HandleSigma1() performs ECDH + ECDSA sign synchronously on
// the CHIP thread, taking ~5.5s on QCC730 (Cortex-M33 @80MHz, soft mbedTLS).
// The default II=500ms causes chip-tool to retransmit Sigma1 ~4 times while
// the device is computing Sigma2, creating a retransmit storm.
//
// Setting II=5000ms gives the device 5.5s before chip-tool's first Sigma1
// retry, eliminating duplicate Sigma1 processing.
//
// NOTE: After Fix 4 (HW crypto) is verified, this can be reduced back to
// the default 500ms since CASE Sigma2 will complete in ~0.1s.
#define CHIP_CONFIG_MRP_LOCAL_IDLE_RETRY_INTERVAL (5000_ms32)
