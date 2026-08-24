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

#pragma once

/**
 * @file qcc730mi-mbedtls-perf.h
 *
 * mbedTLS ECC performance overrides for QCC730MI.
 * Included at the END of Zephyr's config-mbedtls.h via
 * CONFIG_MBEDTLS_USER_CONFIG_FILE.
 *
 * Root cause of SPAKE2+ 14-second hang:
 *   Zephyr's config-mbedtls.h sets worst-case ECC performance values:
 *     MBEDTLS_MPI_WINDOW_SIZE   = 1  (slowest big-integer arithmetic)
 *     MBEDTLS_ECP_WINDOW_SIZE   = 2  (slowest scalar multiplication)
 *     MBEDTLS_ECP_FIXED_POINT_OPTIM = 0  (no precomputed table for G)
 *
 *   During PASE HandlePake1(), the device (verifier) performs ~5 P-256
 *   scalar multiplications synchronously on the CHIP thread:
 *     BeginVerifier:   FELoad(w0) + PointLoad(L)
 *     ComputeRoundOne: FEGenerate(xy) + PointAddMul(Y, G, xy, N, w0)
 *     ComputeRoundTwo: FEMul + PointAddMul(Z) + PointMul(V, L, xy)
 *
 *   At 80 MHz Cortex-M33 with the worst-case settings:
 *     ~2.8s per scalar mul × 5 = ~14.3s total
 *
 *   This blocks the CHIP event loop for 14.3s. The WiFi FW beacon miss
 *   counter reaches threshold (30 × 100ms = 3s) and disconnects, killing
 *   the PASE session mid-handshake.
 *
 * These overrides reduce ECC time from ~14.3s to ~4-6s (~2.5x speedup).
 * RAM cost: ~896 bytes extra (negligible; 133 KB free RAM on QCC730).
 *
 * Used together with:
 *   - WiFiManager.cpp: beacon miss threshold raised to 100 (10s window)
 *   - prj.conf: CONFIG_CHIP_DEVICE_SPAKE2_IT=100 (defensive)
 */

/* Software ECC performance overrides.
 *
 * These settings only apply when MBEDTLS_ECP_ALT is NOT defined (i.e. when
 * the QCC730 PKA hardware accelerator is NOT active). When CONFIG_QCRYPTO=y
 * is set, MBEDTLS_ECP_ALT is defined and libcryptoqcc730.a provides hardware-
 * accelerated mbedtls_ecp_mul / mbedtls_ecdsa_sign / mbedtls_ecdsa_verify via
 * the PKA engine. In that case these window-size settings are irrelevant and
 * are skipped to avoid any potential conflict with the ALT struct layout.
 */
#ifndef MBEDTLS_ECP_ALT

/* Big-integer sliding window size.
 * Range: 1-6. Zephyr default: 1 (slowest). Standard default: 2.
 * 1->2: ~1.4x speedup for MPI multiply, +~128 bytes RAM. */
#undef  MBEDTLS_MPI_WINDOW_SIZE
#define MBEDTLS_MPI_WINDOW_SIZE  2

/* ECP scalar multiplication window size.
 * Range: 2-7. Zephyr default: 2 (slowest). Standard default: 4.
 * 2->4: ~1.5x speedup for scalar mul, +~256 bytes RAM. */
#undef  MBEDTLS_ECP_WINDOW_SIZE
#define MBEDTLS_ECP_WINDOW_SIZE  4

/* Fixed-point speed-up for base-point G multiplications (G*k).
 * 0=disabled (Zephyr default), 1=enabled (standard default).
 * Precomputes a table for G, making G*k ~2x faster.
 * Cost: ~512 bytes RAM for the precomputed table. */
#undef  MBEDTLS_ECP_FIXED_POINT_OPTIM
#define MBEDTLS_ECP_FIXED_POINT_OPTIM  1

#endif /* !MBEDTLS_ECP_ALT */

