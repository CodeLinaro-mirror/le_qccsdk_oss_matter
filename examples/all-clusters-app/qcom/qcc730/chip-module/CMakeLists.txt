# Copyright (c) 2022-2024 Project CHIP Authors
# License: Apache-2.0

if(CONFIG_CHIP)
    include(ExternalProject)

    if(NOT CHIP_ROOT)
        get_filename_component(CHIP_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../../.. REALPATH)
    endif()

    include(${CHIP_ROOT}/config/zephyr/zephyr-util.cmake)

    # QCC730: use the qcom-specific GN args (chip_device_platform = "qcom").
    # Falls back to the generic Zephyr chip-gn directory for toolchain/BUILD.gn.
    get_filename_component(GN_ROOT_TARGET ${CHIP_ROOT}/config/zephyr/chip-gn REALPATH)
    get_filename_component(QCC730_GN_ARGS  ${CHIP_ROOT}/src/platform/qcom/QCC730/args.gni REALPATH)
    get_filename_component(COMMON_CMAKE_SOURCE_DIR ${CHIP_ROOT}/config/common/cmake REALPATH)

    include(${COMMON_CMAKE_SOURCE_DIR}/chip_gn_args.cmake)
    matter_add_gn_arg_string("chip_device_platform" "qcom")
    include(${COMMON_CMAKE_SOURCE_DIR}/chip_gn.cmake)

    if(CONFIG_POSIX_API)
        matter_add_flags(-D_DEFAULT_SOURCE)
        matter_add_flags(-isystem${ZEPHYR_BASE}/include/zephyr/posix)
    endif()

    if(CONFIG_MBEDTLS)
        zephyr_include_directories($<TARGET_PROPERTY:mbedTLS,INTERFACE_INCLUDE_DIRECTORIES>)
        zephyr_compile_definitions($<TARGET_PROPERTY:mbedTLS,INTERFACE_COMPILE_DEFINITIONS>)
    endif()

    zephyr_get_compile_flags(ZEPHYR_CFLAGS_C C)
    matter_add_cflags("${ZEPHYR_CFLAGS_C}")
    # Pass the application source directory as an include path to the GN build
    # so that CONFIG_MBEDTLS_USER_CONFIG_FILE ("qcc730mi-mbedtls-perf.h") can
    # be found when compiling CHIPCryptoPALmbedTLS.cpp inside libCHIP.a.
    matter_add_cflags("-I${APPLICATION_SOURCE_DIR}")
    matter_add_cxxflags("-I${APPLICATION_SOURCE_DIR}")

    # Fix 4: Pass QCC730 HW crypto ALT defines to the GN-compiled Matter code.
    #
    # Background: CONFIG_QCRYPTO=y sets MBEDTLS_ECP_ALT (and other ALT defines)
    # via qcrypto/CMakeLists.txt using zephyr_compile_definitions(). This only
    # affects Zephyr CMake-compiled code. The Matter crypto code
    # (CHIPCryptoPALmbedTLS.cpp) is compiled by GN, not Zephyr CMake, so it
    # does NOT receive these defines automatically.
    #
    # Without MBEDTLS_ECP_ALT in the GN build, CHIPCryptoPALmbedTLS.cpp uses
    # the standard soft mbedTLS ECC (slow, ~5-13s per operation). With it, the
    # linker resolves mbedtls_ecp_mul / mbedtls_ecdsa_sign / mbedtls_ecdsa_verify
    # to the PKA hardware implementations in libcryptoqcc730.a (~0.05-0.1s).
    #
    # The defines must match exactly what qcrypto/CMakeLists.txt sets:
    #   MBEDTLS_ECP_ALT, MBEDTLS_AES_ALT, MBEDTLS_CCM_ALT,
    #   MBEDTLS_SHA1_ALT, MBEDTLS_SHA256_ALT, MBEDTLS_DHM_ALT, MBEDTLS_RSA_ALT
    # CONFIG_SOC_SERIES_QCC730=1 is required by libcryptoqcc730.a internals.
    #
    # The qcrypto/inc/ path provides ecp_alt.h (mbedtls_ecp_group struct layout
    # override) which must be visible when compiling CHIPCryptoPALmbedTLS.cpp.

    zephyr_get_compile_flags(ZEPHYR_CFLAGS_CC CXX)
    matter_add_cxxflags("${ZEPHYR_CFLAGS_CC}")

    if(CONFIG_QCRYPTO)
        # Pass ONLY MBEDTLS_ECP_ALT to the GN build (libCHIP.a).
        #
        # MBEDTLS_ECP_ALT routes mbedtls_ecp_mul/muladd to the QCC730 PKA
        # hardware accelerator (libcryptoqcc730.a, ecp_alt.c). PKA is
        # initialized by pka_sys_init() at SYS_INIT priority 90, which runs
        # before main() — so it is always ready when CHIPCryptoPALmbedTLS.cpp
        # calls ecp_mul during CASE/PASE.
        #
        # SHA256_ALT, SHA1_ALT, AES_ALT, CCM_ALT are intentionally NOT passed
        # to the GN build. These route through CeMLInit() (CE hardware: clocks,
        # DMA, BAM) which is initialized lazily on first use. CeMLInit() fails
        # when called during Server::Init() at boot (CE hardware not yet ready),
        # causing VerifyOrDie(mCommissioningHash.Begin()) → abort() crash.
        # SW SHA256/AES-CCM is fast (~microseconds) — no performance impact.
        #
        # DHM_ALT and RSA_ALT are also excluded: Matter does not use DHM, and
        # RSA_ALT is handled separately by mbedtls_rsa_shim.c.
        #
        # Performance impact of keeping ECP_ALT only:
        #   SPAKE2+ ecp_mul x5:  ~14s SW  → ~0.3s  HW  (~47x)
        #   CASE ecdsa_sign:     ~5.5s SW → ~0.05s HW  (~110x)
        #   CASE ecdsa_verify x2:~13s SW  → ~0.1s  HW  (~130x)
        #   AES-CCM / SHA256:    SW (~µs) — unchanged, no regression vs v1.0
        # zephyr_get_compile_flags() (called above) captures ALL Zephyr compile
        # flags including -DMBEDTLS_SHA256_ALT, -DMBEDTLS_AES_ALT, etc. set by
        # qcrypto/CMakeLists.txt via zephyr_compile_definitions(). We must
        # explicitly undefine the SHA/AES/DHM/RSA ALT flags so that libCHIP.a
        # uses SW implementations for those algorithms (avoiding CeMLInit crash).
        # Only MBEDTLS_ECP_ALT is kept — it routes ecp_mul to PKA hardware which
        # is safely initialized by pka_sys_init() at SYS_INIT before main().
        set(QCRYPTO_UNDEFINES
            "-UMBEDTLS_SHA1_ALT"
            "-UMBEDTLS_SHA256_ALT"
            "-UMBEDTLS_SHA_ALT"
            "-UMBEDTLS_AES_ALT"
            "-UMBEDTLS_CCM_ALT"
            "-UMBEDTLS_DHM_ALT"
            "-UMBEDTLS_RSA_ALT"
        )
        set(QCRYPTO_ECP_DEFINES
            "-DMBEDTLS_ECP_ALT"
            "-DCONFIG_SOC_SERIES_QCC730=1"
        )
        set(QCRYPTO_INC "-I${ZEPHYR_HAL_QCOM_MODULE_DIR}/qcrypto/inc")
        foreach(flag ${QCRYPTO_UNDEFINES} ${QCRYPTO_ECP_DEFINES})
            matter_add_cflags("${flag}")
            matter_add_cxxflags("${flag}")
        endforeach()
        matter_add_cflags("${QCRYPTO_INC}")
        matter_add_cxxflags("${QCRYPTO_INC}")
        message(STATUS "QCC730 HW crypto: MBEDTLS_ECP_ALT only for GN build; SHA/AES/DHM/RSA ALT undefined")
    endif()
    zephyr_get_gnu_cpp_standard(ZEPHYR_GNU_CPP_STD)
    matter_add_cxxflags("${ZEPHYR_GNU_CPP_STD}")

    if(CONFIG_CHIP_OPENTHREAD_CONFIG)
        get_filename_component(CHIP_OPENTHREAD_CONFIG
            ${CONFIG_CHIP_OPENTHREAD_CONFIG}
            REALPATH
            BASE_DIR ${CMAKE_SOURCE_DIR}
        )
        zephyr_set_openthread_config(${CHIP_OPENTHREAD_CONFIG})
    endif()

    matter_common_gn_args(
        DEBUG CONFIG_DEBUG
        LIB_SHELL CONFIG_CHIP_LIB_SHELL
        LIB_TESTS CONFIG_CHIP_BUILD_TESTS
        PROJECT_CONFIG ${CONFIG_CHIP_PROJECT_CONFIG}
    )

    matter_add_gn_arg_string("zephyr_ar" ${CMAKE_AR})
    matter_add_gn_arg_string("zephyr_cc" ${CMAKE_C_COMPILER})
    matter_add_gn_arg_string("zephyr_cxx" ${CMAKE_CXX_COMPILER})
    matter_add_gn_arg_bool("chip_logging" CONFIG_LOG)
    matter_add_gn_arg_bool("chip_error_logging" CONFIG_MATTER_LOG_LEVEL GREATER_EQUAL 1)
    matter_add_gn_arg_bool("chip_progress_logging" CONFIG_MATTER_LOG_LEVEL GREATER_EQUAL 3)
    matter_add_gn_arg_bool("chip_detail_logging" CONFIG_MATTER_LOG_LEVEL GREATER_EQUAL 4)
    matter_add_gn_arg_bool("chip_automation_logging" FALSE)
    matter_add_gn_arg_bool("chip_enable_openthread" CONFIG_NET_L2_OPENTHREAD)
    matter_add_gn_arg_bool("chip_openthread_ftd" CONFIG_OPENTHREAD_FTD)
    matter_add_gn_arg_bool("chip_inet_config_enable_ipv4" CONFIG_CHIP_IPV4)
    matter_add_gn_arg_bool("chip_enable_wifi" CONFIG_WIFI)
    matter_add_gn_arg_bool("chip_config_network_layer_ble" CONFIG_BT)
    matter_add_gn_arg_bool("chip_mdns_minimal" CONFIG_WIFI)
    matter_add_gn_arg_bool("chip_mdns_minimal" CONFIG_NET_L2_ETHERNET)
    matter_add_gn_arg_bool("chip_mdns_platform" CONFIG_NET_L2_OPENTHREAD)
    matter_add_gn_arg_bool("chip_inet_config_enable_tcp_endpoint" CONFIG_CHIP_BUILD_TESTS)
    matter_add_gn_arg_bool("chip_malloc_sys_heap" CONFIG_CHIP_MALLOC_SYS_HEAP)
    matter_add_gn_arg_bool("chip_inet_config_enable_tcp_endpoint" FALSE)
    matter_add_gn_arg_bool("chip_enable_read_client" CONFIG_CHIP_ENABLE_READ_CLIENT)
    matter_add_gn_arg_bool("chip_enable_ota_requestor" CONFIG_CHIP_OTA_REQUESTOR)

    if(BOARD STREQUAL "native_sim")
        matter_add_gn_arg_string("target_cpu" "x86")
    elseif(BOARD STREQUAL "native_sim/native/64")
        matter_add_gn_arg_string("target_cpu" "x64")
    endif()

    if(CONFIG_CHIP_ENABLE_DNSSD_SRP)
        matter_add_gn_arg_string("chip_mdns" "platform")
    endif()

    if(CONFIG_WIFI)
        matter_add_gn_arg_string("chip_mdns" "minimal")
    elseif(CONFIG_NET_L2_ETHERNET)
        matter_add_gn_arg_string("chip_mdns" "minimal")
    elseif(CONFIG_NET_L2_OPENTHREAD)
        matter_add_gn_arg_string("chip_mdns" "platform")
    else()
        matter_add_gn_arg_string("chip_mdns" "none")
    endif()

    if(CONFIG_CHIP_PW_RPC)
        set(PIGWEED_DIR "//third_party/pigweed/repo")
        matter_add_gn_arg_string("pw_assert_BACKEND" ${PIGWEED_DIR}/pw_assert_log:check_backend)
        matter_add_gn_arg_string("pw_log_BACKEND" ${PIGWEED_DIR}/pw_log_basic)
        matter_add_gn_arg("pw_build_LINK_DEPS" [\"${PIGWEED_DIR}/pw_assert:impl\",\ \"${PIGWEED_DIR}/pw_log:impl\"])
    endif()

    matter_generate_args_tmp_file()

    matter_build(chip
        LIB_SHELL ${CONFIG_CHIP_LIB_SHELL}
        LIB_TESTS ${CONFIG_CHIP_BUILD_TESTS}
        GN_DEPENDENCIES kernel
    )
    set_property(GLOBAL APPEND PROPERTY ZEPHYR_INTERFACE_LIBS chip)
endif()
