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

#include <zephyr/kernel.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_event.h>

class WiFiManager
{
public:
    static WiFiManager & Instance();

    /**
     * Initialize WiFi manager
     */
    void Init();

    /**
     * Connect to WiFi network
     * @param ssid WiFi SSID
     * @param password WiFi password
     * @return 0 on success, negative error code on failure
     */
    int Connect(const char * ssid, const char * password);

    /**
     * Disconnect from WiFi network
     * @return 0 on success, negative error code on failure
     */
    int Disconnect();

    /**
     * Check if WiFi is connected
     * @return true if connected, false otherwise
     */
    bool IsConnected();

    /**
     * Get WiFi SSID
     * @param ssid Buffer to store SSID
     * @param ssid_len Buffer length
     * @return 0 on success, negative error code on failure
     */
    int GetSSID(char * ssid, size_t ssid_len);

private:
    WiFiManager() = default;
    ~WiFiManager() = default;

    WiFiManager(const WiFiManager &) = delete;
    WiFiManager & operator=(const WiFiManager &) = delete;

    /**
     * WiFi event handler callback
     */
    static void WiFiEventHandler(struct net_mgmt_event_callback * cb, uint32_t mgmt_event, struct net_if * iface);

    struct net_mgmt_event_callback mWiFiCallback;
    bool mIsConnected = false;
    char mSSID[WIFI_SSID_MAX_LEN + 1] = { 0 };
};

