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

// WiFi support via qcom's forked Zephyr WiFi layer (net_mgmt / WiFiManager).
// Forked from ../../Zephyr/wifi/ into qcom/common/wifi/ because QCC730 needs
// QCC730-specific changes (channelHint parameter, NAN single-radio concurrency
// calls) that must not contaminate files shared with nRF Connect / Telink builds.
// See BUILD.gn for the corresponding source-file rerouting.
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
#include "wifi/ConnectivityManagerImplWiFi.h"
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_WIFIPAF
#include <wifipaf/WiFiPAFLayer.h>
#include <wifipaf/WiFiPAFRole.h>
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

public:
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI && CHIP_DEVICE_CONFIG_ENABLE_WIFIPAF
    void WiFiPafCancelActivePublish();
    uint16_t WiFiPafGetApFreq() const { return mApFreq; }
#endif

private:
    CHIP_ERROR _Init(void);
    void _OnPlatformEvent(const ChipDeviceEvent * event);

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI && CHIP_DEVICE_CONFIG_ENABLE_WIFIPAF
    // -- WiFi-PAF (NAN USD) commissioning ------------------------------------
    // QCC730 is a commissionee (Publisher).  Subscriber-side methods are
    // intentionally stubbed -- this platform has no BLE and is never a
    // commissioner.

    CHIP_ERROR _WiFiPAFPublish(ConnectivityManager::WiFiPAFAdvertiseParam & args);
    CHIP_ERROR _WiFiPAFCancelPublish(uint32_t PublishId);

    // Subscriber side -- not used on a commissionee, stub to NOT_IMPLEMENTED.
    CHIP_ERROR _WiFiPAFSubscribe(const uint16_t & connDiscriminator, void * appState,
                                 OnConnectionCompleteFunct onSuccess,
                                 OnConnectionErrorFunct onError);
    CHIP_ERROR _WiFiPAFCancelSubscribe(uint32_t SubscribeId);
    CHIP_ERROR _WiFiPAFCancelIncompleteSubscribe();

    CHIP_ERROR _WiFiPAFSend(const WiFiPAF::WiFiPAFSession & TxInfo,
                             chip::System::PacketBufferHandle && msgBuf);
    CHIP_ERROR _WiFiPAFShutdown(uint32_t id, WiFiPAF::WiFiPafRole role);

    // Always true -- QCC730 has dedicated NAN hardware via qcom_wifi_drv.
    bool _WiFiPAFResourceAvailable() { return true; }

    // Store the commissioner's AP frequency so _WiFiPAFPublish can use the
    // same channel.  0 = use default 2437 MHz (CH6).
    void _WiFiPafSetApFreq(const uint16_t freq) { mApFreq = freq; }

    // C-compatible callback registered with qcc730_nan_de_glue.
    // Called from the hostap eloop thread; must PostEventOrDie() before
    // touching any Matter APIs.
    static void OnNanReceiveCallback(int id, int peer_instance_id,
                                     const uint8_t * ssi, size_t ssi_len,
                                     const uint8_t * peer_addr);

    uint16_t mApFreq = 0;
    uint32_t mPublishId = 0;        // active NAN publish id; 0 = none
    // True once the WiFiPAFEndPoint has been created for the active publish session.
    // Reset in _WiFiPAFShutdown so a new commissioning cycle creates a fresh endpoint.
    bool mWiFiPAFEndpointCreated = false;
#endif // CHIP_DEVICE_CONFIG_ENABLE_WIFI && CHIP_DEVICE_CONFIG_ENABLE_WIFIPAF

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
