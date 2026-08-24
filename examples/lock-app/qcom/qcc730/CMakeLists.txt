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
get_filename_component(LOCK_COMMON_DIR ${CHIP_ROOT}/examples/lock-app/lock-common REALPATH)

# Use the local chip-module wrapper (no openthread dependency) instead of
# the SDK's config/zephyr/chip-module which requires openthread.
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/chip-module)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# QCC730 HW Crypto: fix duplicate symbol conflicts caused by CONFIG_QCRYPTO=y.
# (Same rationale as all-clusters-app — see that file for full explanation.)
if(CONFIG_QCRYPTO)
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
    zephyr_link_libraries(-Wl,--allow-multiple-definition)
    message(STATUS "QCC730 HW Crypto: ECDSA->HW PKA; SHA/AES/CCM->SW (CeMLInit-safe)")
endif()

project(qcc730mi-lock-app)

# Make the application source directory visible to the Zephyr build system
# so that files referenced by CONFIG_MBEDTLS_USER_CONFIG_FILE can be found.
zephyr_include_directories(${APPLICATION_SOURCE_DIR})

include(${CHIP_ROOT}/config/zephyr/app/enable-gnu-std.cmake)
include(${CHIP_ROOT}/src/app/chip_data_model.cmake)
include(${LOCK_COMMON_DIR}/lock-app.cmake)

target_include_directories(app PRIVATE
    main/include
    ${LOCK_COMMON_DIR}/include
    ${GEN_DIR}/app-common
    ${GEN_DIR}/lock-app
    ${CHIP_ROOT}/src/include
)

# Ring service include path (qring module provides ring_service.h)
if(DEFINED ZEPHYR_BASE)
    get_filename_component(WORKSPACE_ROOT ${ZEPHYR_BASE}/.. REALPATH)
    target_include_directories(app PRIVATE
        ${WORKSPACE_ROOT}/modules/hal/qcom/qring/inc
    )
endif()

target_sources(app PRIVATE
    main/main.cpp
    main/AppTask.cpp
    main/mbedtls_rsa_shim.c
    main/QcomLockManager.cpp
    main/ZclCallbacks.cpp
    ${LOCK_COMMON_DIR}/src/Identify.cpp
)

# lock_fw.cpp is only needed in Host mode (ring service + PM wakeup)
if(CONFIG_LOCK_HOST_MODE)
    target_sources(app PRIVATE main/lock_fw.cpp)
endif()

# Add lock-common sources: LockManager + LockEndpoint only.
# ZCLDoorLockCallbacks is NOT included here — we use our own QCC730 version above.
target_sources(app PRIVATE
    ${LOCK_COMMON_DIR}/src/LockManager.cpp
    ${LOCK_COMMON_DIR}/src/LockEndpoint.cpp
)

chip_configure_data_model(app
    ZAP_FILE ${CMAKE_CURRENT_SOURCE_DIR}/qcc730-lock-app.zap
    IDL ${LOCK_COMMON_DIR}/lock-app.matter
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
        "         zephyr_HASHED.elf will NOT be generated."
    )
endif()

# ===========================================================================
# Optional 'sign' target  (west build -t sign)
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
