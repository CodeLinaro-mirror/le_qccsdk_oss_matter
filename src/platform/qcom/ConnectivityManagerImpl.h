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
 *          Provides an implementation of the ConnectivityManager singleton
 *          for the QCC730 platform.
 *
 *          QCC730 is a WiFi-only SoC running Zephyr RTOS.  The Zephyr WiFi
 *          driver (qcom_wifi_drv / qcom_wifi_mgmt) raises standard Zephyr
 *          net_mgmt events (NET_EVENT_WIFI_CONNECT_RESULT, etc.), so the
 *          generic Zephyr WiFi platform layer can be reused directly.
 *
 *          The only QCC730-specific customisation is:
 *            - BLE and Thread are disabled.
 *            - The event-queue size is raised to 200 (see CHIPDevicePlatformConfig.h)
 *              to prevent PostEventOrDie() abort() during WiFi connect.
 */

#pragma once

#include <platform/ConnectivityManager.h>
#include <platform/internal/GenericConnectivityManagerImpl.h>
#include <platform/internal/GenericConnectivityManagerImpl_NoThread.h>
#include <platform/internal/GenericConnectivityManagerImpl_NoBLE.h>
#include <platform/internal/GenericConnectivityManagerImpl_NoWiFi.h>
#include <platform/internal/GenericConnectivityManagerImpl_UDP.h>
#if INET_CONFIG_ENABLE_TCP_ENDPOINT
#include <platform/internal/GenericConnectivityManagerImpl_TCP.h>
#endif

// WiFi support via the generic Zephyr WiFi layer (net_mgmt / WiFiManager).
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
#include <platform/Zephyr/wifi/ConnectivityManagerImplWiFi.h>
#endif

#include <lib/support/logging/CHIPLogging.h>

namespace chip {
namespace Inet {
class IPAddress;
} // namespace Inet
} // namespace chip

namespace chip {
namespace DeviceLayer {

/**
 * Concrete implementation of the ConnectivityManager singleton for QCC730.
 *
 * Inherits:
 *   - Generic UDP/TCP connectivity
 *   - No BLE (QCC730 has no BLE hardware)
 *   - No Thread
 *   - Zephyr WiFi implementation (ConnectivityManagerImplWiFi)
 */
class ConnectivityManagerImpl final
    : public ConnectivityManager,
      public Internal::GenericConnectivityManagerImpl<ConnectivityManagerImpl>,
      public Internal::GenericConnectivityManagerImpl_UDP<ConnectivityManagerImpl>,
#if INET_CONFIG_ENABLE_TCP_ENDPOINT
      public Internal::GenericConnectivityManagerImpl_TCP<ConnectivityManagerImpl>,
#endif
      public Internal::GenericConnectivityManagerImpl_NoBLE<ConnectivityManagerImpl>,
      public Internal::GenericConnectivityManagerImpl_NoThread<ConnectivityManagerImpl>,
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
      public ConnectivityManagerImplWiFi
#else
      public Internal::GenericConnectivityManagerImpl_NoWiFi<ConnectivityManagerImpl>
#endif
{
    friend class ConnectivityManager;

private:
    CHIP_ERROR _Init(void);
    void _OnPlatformEvent(const ChipDeviceEvent * event);

    friend ConnectivityManager & ConnectivityMgr(void);
    friend ConnectivityManagerImpl & ConnectivityMgrImpl(void);

    static ConnectivityManagerImpl sInstance;
};

inline ConnectivityManager & ConnectivityMgr(void)
{
    return ConnectivityManagerImpl::sInstance;
}

inline ConnectivityManagerImpl & ConnectivityMgrImpl(void)
{
    return ConnectivityManagerImpl::sInstance;
}

} // namespace DeviceLayer
} // namespace chip
