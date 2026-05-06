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
 *          ConnectivityManager implementation for QCC730 (Zephyr/WiFi-only).
 *
 *          QCC730 has no BLE and no Thread, so those generic template
 *          instantiations are omitted.  WiFi is handled by the Zephyr
 *          platform layer (ConnectivityManagerImplWiFi / WiFiManager)
 *          which listens to standard Zephyr net_mgmt events raised by
 *          the qcom_wifi_drv driver.
 */

#include <platform/internal/CHIPDeviceLayerInternal.h>

#include <inet/UDPEndPointImplSockets.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/ConnectivityManager.h>
#include <platform/Zephyr/InetUtils.h>

#include <zephyr/net/net_if.h>

#include <platform/internal/GenericConnectivityManagerImpl_UDP.ipp>

#if INET_CONFIG_ENABLE_TCP_ENDPOINT
#include <platform/internal/GenericConnectivityManagerImpl_TCP.ipp>
#endif

// QCC730 has no BLE and no Thread — do not include those .ipp files.

using namespace ::chip::Inet;
using namespace ::chip::DeviceLayer::Internal;

namespace chip {
namespace DeviceLayer {

namespace {

/**
 * Helper: join or leave an IPv6 multicast group on a Zephyr net_if.
 * Used by the UDP multicast group handler below.
 */
static CHIP_ERROR JoinLeaveMulticastGroup(net_if * iface, const Inet::IPAddress & address,
                                          UDPEndPointImplSockets::MulticastOperation operation)
{
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    const in6_addr in6Addr = InetUtils::ToZephyrAddr(address);

    if (operation == UDPEndPointImplSockets::MulticastOperation::kJoin)
    {
        net_if_mcast_addr * maddr = net_if_ipv6_maddr_add(iface, &in6Addr);
        if (maddr && !net_if_ipv6_maddr_is_joined(maddr))
        {
            net_if_ipv6_maddr_join(iface, maddr);
        }
    }
    else if (operation == UDPEndPointImplSockets::MulticastOperation::kLeave)
    {
        VerifyOrReturnError(net_if_ipv6_maddr_rm(iface, &in6Addr), CHIP_ERROR_INVALID_ADDRESS);
    }
    else
    {
        return CHIP_ERROR_INCORRECT_STATE;
    }
#endif // CHIP_DEVICE_CONFIG_ENABLE_WIFI

    return CHIP_NO_ERROR;
}

} // namespace

ConnectivityManagerImpl ConnectivityManagerImpl::sInstance;

CHIP_ERROR ConnectivityManagerImpl::_Init()
{
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    // Initialise the Zephyr WiFi platform layer (registers net_mgmt callbacks,
    // sets up the WiFiManager state machine, etc.).
    ReturnErrorOnFailure(InitWiFi());
#endif

    // Register the multicast group handler so that Matter can join/leave
    // IPv6 multicast groups on the WiFi interface.
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    UDPEndPointImplSockets::SetMulticastGroupHandler(
        [](InterfaceId interfaceId, const IPAddress & address,
           UDPEndPointImplSockets::MulticastOperation operation) -> CHIP_ERROR {
            if (interfaceId.IsPresent())
            {
                net_if * iface = InetUtils::GetInterface(interfaceId);
                VerifyOrReturnError(iface != nullptr, INET_ERROR_UNKNOWN_INTERFACE);
                return JoinLeaveMulticastGroup(iface, address, operation);
            }

            // No specific interface: apply to all interfaces.
            for (int i = 1; net_if * iface = net_if_get_by_index(i); i++)
            {
                ReturnErrorOnFailure(JoinLeaveMulticastGroup(iface, address, operation));
            }
            return CHIP_NO_ERROR;
        });
#endif // CHIP_DEVICE_CONFIG_ENABLE_WIFI

    return CHIP_NO_ERROR;
}

void ConnectivityManagerImpl::_OnPlatformEvent(const ChipDeviceEvent * event)
{
    // QCC730 has no Thread; nothing extra to forward here.
    (void) event;
}

} // namespace DeviceLayer
} // namespace chip
