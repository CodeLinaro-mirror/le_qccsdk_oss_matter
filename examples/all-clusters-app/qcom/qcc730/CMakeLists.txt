# Copyright (c) 2024 Project CHIP Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.20.0)

# CHIP_ROOT is the connectedhomeip SDK root (4 levels up from qcc730/).
get_filename_component(CHIP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../.. REALPATH)
get_filename_component(GEN_DIR ${CHIP_ROOT}/zzz_generated/ REALPATH)
get_filename_component(ALL_CLUSTERS_COMMON_DIR ${CHIP_ROOT}/examples/all-clusters-app/all-clusters-common REALPATH)

# Use the local chip-module wrapper (no openthread dependency) instead of
# the SDK's config/zephyr/chip-module which requires openthread.
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/chip-module)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# QCC730 HW Crypto: fix duplicate symbol conflicts caused by CONFIG_QCRYPTO=y.
#
# qcrypto/CMakeLists.txt uses zephyr_compile_definitions() which adds
# -DMBEDTLS_RSA_ALT and -DMBEDTLS_ECP_ALT (but NOT MBEDTLS_ECDSA_SIGN_ALT)
# to ALL Zephyr targets including libmbedTLSBase.a. This causes two problems
# when Matter SDK is used (which enables MBEDTLS_ECDSA_C and MBEDTLS_RSA_C):
#
# Problem 1: ecdsa.c compiled WITHOUT MBEDTLS_ECDSA_SIGN_ALT defines
#   mbedtls_ecdsa_sign/verify in libmbedTLSBase.a. libcryptoqcc730.a also
#   defines them in ecdsa_alt.c.obj -> duplicate symbol linker error.
#   Fix: add MBEDTLS_ECDSA_SIGN_ALT + MBEDTLS_ECDSA_VERIFY_ALT to mbedTLS
#   targets so ecdsa.c skips those functions.
#
# Problem 2: MBEDTLS_RSA_ALT causes rsa.c to exclude mbedtls_rsa_get_bitlen
#   (inside #if !defined(MBEDTLS_RSA_ALT)). pk_wrap.c calls it but the blob's
#   rsa_alt.c doesn't provide it (older mbedTLS API). Matter never uses RSA.
#   Fix: remove MBEDTLS_RSA_ALT from mbedTLS targets so rsa.c compiles fully.
#
# We use target_compile_definitions() on the mbedTLS library targets directly,
# which is processed after find_package(Zephyr) creates those targets.
# The REMOVE_ITEM approach uses generator expressions to negate the define.
if(CONFIG_QCRYPTO)
    # qcrypto/CMakeLists.txt sets MBEDTLS_SHA256_ALT, MBEDTLS_SHA1_ALT,
    # MBEDTLS_AES_ALT, MBEDTLS_CCM_ALT for ALL Zephyr targets via
    # zephyr_compile_definitions(). This causes sha256.c, sha1.c, aes.c, ccm.c
    # to NOT compile their SW implementations into libmbedTLSBase.a.
    # The blob (libcryptoqcc730.a) provides the HW versions instead.
    #
    # Problem: HW SHA/AES calls CeMLInit() which initializes CE hardware
    # (clocks, DMA, BAM) lazily. CeMLInit() FAILS when called during
    # Server::Init() at boot (CE hardware not ready), causing:
    #   mCommissioningHash.Begin() -> mbedtls_sha256_starts -> qcom_hash_starts
    #   -> crypto_qcc_digest_init -> CeMLInit() -> FAILS -> CHIP_ERROR_INTERNAL
    #   -> VerifyOrDie -> abort() -> kernel panic
    #
    # Fix: undefine SHA256_ALT, SHA1_ALT, AES_ALT, CCM_ALT on the mbedTLS
    # library targets so sha256.c etc. compile their SW implementations into
    # libmbedTLSBase.a. Since libmbedTLSBase.a is linked BEFORE libcryptoqcc730.a,
    # with --allow-multiple-definition the SW versions win at link time.
    # SW SHA256/AES-CCM is fast (~microseconds) — no performance regression.
    #
    # ECDSA_SIGN_ALT + ECDSA_VERIFY_ALT are ADDED so ecdsa.c skips SW sign/verify
    # and the blob HW PKA versions are used. PKA is initialized by pka_sys_init()
    # at SYS_INIT before main() — safe to call at any time.
    foreach(mbedtls_target mbedTLSBase mbedTLSCrypto mbedTLSX509)
        target_compile_definitions(${mbedtls_target} PRIVATE
            MBEDTLS_ECDSA_SIGN_ALT
            MBEDTLS_ECDSA_VERIFY_ALT
        )
        target_compile_options(${mbedtls_target} PRIVATE
            -UMBEDTLS_SHA256_ALT
            -UMBEDTLS_SHA1_ALT
            -UMBEDTLS_SHA_ALT
            -UMBEDTLS_AES_ALT
            -UMBEDTLS_CCM_ALT
        )
    endforeach()
    # --allow-multiple-definition: blob defines sha256_starts, aes_init, ccm_init
    # etc. AND libmbedTLSBase.a now also defines them (SW). Linker uses first
    # occurrence (libmbedTLSBase.a, before libcryptoqcc730.a) for SHA/AES/CCM.
    # ecdsa_sign/verify: suppressed in libmbedTLSBase.a by ECDSA_SIGN_ALT,
    # so blob HW PKA versions are used for those critical ECC operations.
    zephyr_link_libraries(-Wl,--allow-multiple-definition)
    message(STATUS "QCC730 HW Crypto: ECDSA->HW PKA; SHA/AES/CCM->SW (CeMLInit-safe)")
endif()


project(qcc730mi-all-clusters-app)

# Make the application source directory visible to the Zephyr build system
# so that files referenced by CONFIG_MBEDTLS_USER_CONFIG_FILE (e.g.
# qcc730mi-mbedtls-perf.h) can be found via the compiler include path.
zephyr_include_directories(${APPLICATION_SOURCE_DIR})

include(${CHIP_ROOT}/config/zephyr/app/enable-gnu-std.cmake)
include(${CHIP_ROOT}/src/app/chip_data_model.cmake)

target_include_directories(app PRIVATE
    main/include
    ${ALL_CLUSTERS_COMMON_DIR}/include
    ${GEN_DIR}/app-common
    ${GEN_DIR}/all-clusters-app
    ${CHIP_ROOT}/src/include
)

target_sources(app PRIVATE
    main/main.cpp
    main/AppTask.cpp
    main/DeviceCallbacks.cpp
    main/Globals.cpp
    main/LEDWidget.cpp
    # main/ShellCommands.cpp
    main/WiFiManager.cpp
    main/mbedtls_rsa_shim.c

    ${ALL_CLUSTERS_COMMON_DIR}/src/binding-handler.cpp
    # ${ALL_CLUSTERS_COMMON_DIR}/src/bridged-actions-stub.cpp  # removed: cluster disabled
    # ${ALL_CLUSTERS_COMMON_DIR}/src/static-supported-modes-manager.cpp  # removed: cluster disabled
    # ${ALL_CLUSTERS_COMMON_DIR}/src/static-supported-temperature-levels.cpp  # removed: cluster disabled
    # ${ALL_CLUSTERS_COMMON_DIR}/src/fan-stub.cpp  # removed: cluster disabled
    # ${ALL_CLUSTERS_COMMON_DIR}/src/smco-stub.cpp  # removed: cluster disabled
    # ${ALL_CLUSTERS_COMMON_DIR}/src/resource-monitoring-delegates.cpp  # removed: cluster disabled
    # ${ALL_CLUSTERS_COMMON_DIR}/src/air-quality-instance.cpp  # removed: cluster disabled
    # ${ALL_CLUSTERS_COMMON_DIR}/src/chime-instance.cpp
    # ${ALL_CLUSTERS_COMMON_DIR}/src/concentration-measurement-instances.cpp  # removed: cluster disabled
    # ${ALL_CLUSTERS_COMMON_DIR}/src/oven-modes.cpp  # removed: cluster disabled
    # ${ALL_CLUSTERS_COMMON_DIR}/src/tls-certificate-management-instance.cpp
    # ${ALL_CLUSTERS_COMMON_DIR}/src/tls-client-management-instance.cpp
)

chip_configure_data_model(app
    ZAP_FILE ${CMAKE_CURRENT_SOURCE_DIR}/all-clusters-minimal-app.zap
)

# QCC730 WiFi include paths (needed for WiFiManager)
if(CONFIG_WIFI AND DEFINED ZEPHYR_BASE)
    get_filename_component(WORKSPACE_ROOT ${ZEPHYR_BASE}/.. REALPATH)
    target_include_directories(app PRIVATE
        ${WORKSPACE_ROOT}/modules/hal/qcom/qwifi/inc
        ${WORKSPACE_ROOT}/modules/hal/qcom/qwifi/zephyr
        ${WORKSPACE_ROOT}/qcc730/drivers/wifi/qcom/inc
    )
endif()

# ===========================================================================
# Post-build: generate zephyr_HASHED.elf (required by SBL bootloader)
#
# Mirrors the post-build logic in qapp/qcli_app/CMakeLists.txt.
# ZEPHYR_HAL_QCOM_MODULE_DIR is set automatically by Zephyr's module system
# when the hal_qcom module is present in the workspace.
# ===========================================================================
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(POST_PY      "${ZEPHYR_HAL_QCOM_MODULE_DIR}/tools/post_process.py")
set(HASH_SCRIPT  "${ZEPHYR_HAL_QCOM_MODULE_DIR}/tools/qhash/sechash/createxbl.py")
set(SBL_TOOL_PREFIX "${ZEPHYR_SDK_INSTALL_DIR}/arm-zephyr-eabi/bin/arm-zephyr-eabi")

set(APP_OUT_DIR      "${APPLICATION_BINARY_DIR}/zephyr")
set(HASHED_ELF       "${APP_OUT_DIR}/zephyr_HASHED.elf")

if(EXISTS "${POST_PY}")
    add_custom_command(
        OUTPUT  "${HASHED_ELF}"
        COMMAND "${Python3_EXECUTABLE}" "${POST_PY}"
                --elf         "${APP_OUT_DIR}/zephyr.elf"
                --tool-prefix "${SBL_TOOL_PREFIX}"
                --hash-script "${HASH_SCRIPT}"
                --hashed-out  "${HASHED_ELF}"
                --check-noinit
        DEPENDS zephyr_final "${APP_OUT_DIR}/zephyr.elf"
        COMMENT "[QCC730] Post-processing ELF -> zephyr_HASHED.elf"
        VERBATIM
    )
    add_custom_target(post_process_elf ALL DEPENDS "${HASHED_ELF}")
    message(STATUS "[QCC730] post_process_elf target enabled -> ${HASHED_ELF}")
else()
    message(WARNING
        "[QCC730] post_process.py not found at ${POST_PY}\n"
        "         ZEPHYR_HAL_QCOM_MODULE_DIR=${ZEPHYR_HAL_QCOM_MODULE_DIR}\n"
        "         zephyr_HASHED.elf will NOT be generated."
    )
endif()

# ===========================================================================
# Optional 'sign' target  (west build -t sign)
# Signs both SBL and application ELF with Qualcomm sectools.
# NOT added to ALL — only runs when explicitly requested.
# ===========================================================================
set(SECTOOLS_DIR    "${ZEPHYR_HAL_QCOM_MODULE_DIR}/tools/sectools")
set(SECTOOLS_SCRIPT "${SECTOOLS_DIR}/sectools.py")
set(SECTOOLS_CONFIG "${SECTOOLS_DIR}/config/qcc730/qcc730_secimage.xml")

set(SBL_ELF         "${APPLICATION_BINARY_DIR}/modules/hal_qcom/qboot/${BOARD}_sbl.elf")
set(APP_ELF         "${APP_OUT_DIR}/zephyr.elf")

set(SBL_SIGNED_DIR  "${APPLICATION_BINARY_DIR}/modules/hal_qcom/qboot/zephyr_sec_sbl")
set(APP_SIGNED_DIR  "${APP_OUT_DIR}/zephyr_sec_app")

set(SBL_SIGNED_BIN  "${SBL_SIGNED_DIR}/qcc730/sbl/${BOARD}_sbl.elf")
set(APP_SIGNED_BIN  "${APP_SIGNED_DIR}/qcc730/app/zephyr.elf")

if(EXISTS "${SECTOOLS_SCRIPT}")
    add_custom_command(
        OUTPUT "${SBL_SIGNED_BIN}"
        COMMAND "${Python3_EXECUTABLE}" "${SECTOOLS_SCRIPT}"
                secimage -i "${SBL_ELF}" -c "${SECTOOLS_CONFIG}" -sa -g sbl -o "${SBL_SIGNED_DIR}"
        WORKING_DIRECTORY "${SECTOOLS_DIR}"
        DEPENDS "${SBL_ELF}"
        COMMENT "[QCC730] Signing SBL with sectools"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${APP_SIGNED_BIN}"
        COMMAND "${Python3_EXECUTABLE}" "${SECTOOLS_SCRIPT}"
                secimage -i "${APP_ELF}" -c "${SECTOOLS_CONFIG}" -sa -g app -o "${APP_SIGNED_DIR}"
        WORKING_DIRECTORY "${SECTOOLS_DIR}"
        DEPENDS "${APP_ELF}"
        COMMENT "[QCC730] Signing application with sectools"
        VERBATIM
    )
    add_custom_target(sign
        DEPENDS "${SBL_SIGNED_BIN}" "${APP_SIGNED_BIN}"
        COMMAND ${CMAKE_COMMAND} -E echo "Signed SBL:  ${SBL_SIGNED_DIR}"
        COMMAND ${CMAKE_COMMAND} -E echo "Signed App:  ${APP_SIGNED_DIR}"
        VERBATIM
    )
    if(TARGET post_process_elf)
        add_dependencies(sign post_process_elf)
    endif()
endif()
