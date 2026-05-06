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
 *          NetworkCommissioning WiFiDriver for the Qualcomm QCC730MI.
 *
 *          The QCC730 WiFi driver (qcom_wifi_drv / qcom_wifi_mgmt) exposes
 *          the standard Zephyr net_mgmt WiFi management API and raises the
 *          same NET_EVENT_WIFI_* events as any other Zephyr WiFi driver.
 *          Therefore this driver is a thin alias of ZephyrWifiDriver; it
 *          exists as a named type so that:
 *            1. The platform layer can be identified as "qcom" in logs.
 *            2. Future QCC730-specific extensions (e.g. TX-power, EDCA
 *               parameters via qcom_wifi_mgmt.h) can be added here without
 *               touching the generic Zephyr driver.
 */

#pragma once

#include <platform/Zephyr/wifi/ZephyrWifiDriver.h>

namespace chip {
namespace DeviceLayer {
namespace NetworkCommissioning {

/**
 * QCC730 WiFi NetworkCommissioning driver.
 *
 * Inherits all functionality from ZephyrWifiDriver which uses the standard
 * Zephyr net_mgmt WiFi API.  The QCC730 qcom_wifi_drv raises the same
 * NET_EVENT_WIFI_CONNECT_RESULT / NET_EVENT_WIFI_DISCONNECT_RESULT /
 * NET_EVENT_WIFI_SCAN_RESULT events, so no override is needed.
 */
class QComWifiDriver final : public ZephyrWifiDriver
{
public:
    static QComWifiDriver & Instance()
    {
        static QComWifiDriver sInstance;
        return sInstance;
    }

private:
    QComWifiDriver() = default;
};

} // namespace NetworkCommissioning
} // namespace DeviceLayer
} // namespace chip
