/*
 *
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

#include "ZephyrWifiDriver.h"

#include <stdint.h>

#include <platform/KeyValueStoreManager.h>

#include <lib/support/CodeUtils.h>
#include <lib/support/SafeInt.h>
#include <lib/support/TypeTraits.h>
#include <platform/CHIPDeviceLayer.h>

/* Forward declaration at file scope so C++ uses C linkage.
 * qcc730_nan_de_glue.h lives in the Zephyr BSP include tree which is not
 * visible from the GN build; declare directly to avoid a missing header. */
#ifdef CONFIG_WIFI_NM_WPA_SUPPLICANT_NAN_USD
extern "C" void qcc730_nan_glue_allow_wifi_scan(void);
extern "C" void qcc730_nan_glue_resume_nan_rx(void);
#endif

using namespace ::chip;
using namespace ::chip::DeviceLayer::Internal;
using namespace ::chip::DeviceLayer::PersistedStorage;
using namespace ::chip::app::Clusters::NetworkCommissioning;

namespace chip {
namespace DeviceLayer {
namespace NetworkCommissioning {

size_t ZephyrWifiDriver::WiFiNetworkIterator::Count()
{
    VerifyOrReturnValue(mDriver != nullptr, 0);
    return mDriver->mStagingNetwork.IsConfigured() ? 1 : 0;
}

bool ZephyrWifiDriver::WiFiNetworkIterator::Next(Network & item)
{
    // we assume only one network is actually supported
    // TODO: verify if this can be extended
    if (mExhausted || 0 == Count())
    {
        return false;
    }

    memcpy(item.networkID, mDriver->mStagingNetwork.ssid, mDriver->mStagingNetwork.ssidLen);
    item.networkIDLen = static_cast<uint8_t>(mDriver->mStagingNetwork.ssidLen);
    item.connected    = false;

    mExhausted = true;

    WiFiManager::WiFiInfo wifiInfo;
    if (CHIP_NO_ERROR == WiFiManager::Instance().GetWiFiInfo(wifiInfo))
    {
        if (WiFiManager::StationStatus::CONNECTED <= WiFiManager::Instance().GetStationStatus())
        {
            if (wifiInfo.mSsidLen == item.networkIDLen && 0 == memcmp(wifiInfo.mSsid, item.networkID, wifiInfo.mSsidLen))
            {
                item.connected = true;
            }
        }
    }
    return true;
}

bool ZephyrWifiScanResponseIterator::Next(WiFiScanResponse & item)
{
    if (mResultId < mResultCount)
    {
        item = mResults[mResultId++];
        return true;
    }
    return false;
}

void ZephyrWifiScanResponseIterator::Release()
{
    mResultId = mResultCount = 0;
    Platform::MemoryFree(mResults);
    mResults = nullptr;
}

void ZephyrWifiScanResponseIterator::Add(const WiFiScanResponse & result)
{
    void * newResults = Platform::MemoryRealloc(mResults, (mResultCount + 1) * sizeof(WiFiScanResponse));

    if (newResults)
    {
        mResults                 = static_cast<WiFiScanResponse *>(newResults);
        mResults[mResultCount++] = result;
    }
}

CHIP_ERROR ZephyrWifiDriver::Init(NetworkStatusChangeCallback * networkStatusChangeCallback)
{
    mpNetworkStatusChangeCallback = networkStatusChangeCallback;

    LoadFromStorage();

    if (mStagingNetwork.IsConfigured())
    {
        WiFiManager::ConnectionHandling handling{ [](const wifi_conn_status & connStatus) {
                                                     Instance().OnNetworkConnStatusChanged(connStatus);
                                                 },
                                                  System::Clock::Seconds32{ kWiFiConnectNetworkTimeoutSeconds } };
        ReturnErrorOnFailure(
            WiFiManager::Instance().Connect(mStagingNetwork.GetSsidSpan(), mStagingNetwork.GetPassSpan(), handling));
    }

    return CHIP_NO_ERROR;
}

void ZephyrWifiDriver::OnNetworkConnStatusChanged(const wifi_conn_status & connStatus)
{
    // TODO: check if we can report more accurate errors
    Status status = connStatus ? Status::kUnknownError : Status::kSuccess;

#ifdef CONFIG_WIFI_NM_WPA_SUPPLICANT_NAN_USD
    /* WiFi connection attempt is done (success or failure). Re-enable NAN SDF
     * RX so the next commissioning cycle can receive subscriber SDFs again.
     * This is paired with qcc730_nan_glue_allow_wifi_scan() in ConnectNetwork. */
    qcc730_nan_glue_resume_nan_rx();
#endif

    if (status == Status::kSuccess)
    {
        ConnectivityMgr().SetWiFiStationMode(ConnectivityManager::kWiFiStationMode_Enabled);
    }

    if (mpNetworkStatusChangeCallback)
    {
        const uint8_t * ssid{};
        size_t ssidLen{};
        WiFiManager::WiFiInfo wifiInfo;

        if (CHIP_NO_ERROR == WiFiManager::Instance().GetWiFiInfo(wifiInfo))
        {
            ssid    = wifiInfo.mSsid;
            ssidLen = wifiInfo.mSsidLen;
        }
        else
        {
            ssid    = WiFiManager::Instance().GetWantedNetwork().ssid;
            ssidLen = WiFiManager::Instance().GetWantedNetwork().ssidLen;
        }
        mpNetworkStatusChangeCallback->OnNetworkingStatusChange(status, MakeOptional(ByteSpan(ssid, ssidLen)),
                                                                connStatus ? MakeOptional(static_cast<int32_t>(connStatus))
                                                                           : NullOptional);
    }

    if (mpConnectCallback)
    {
        if (status == Status::kSuccess)
        {
            mpConnectCallback->OnResult(status, CharSpan(), 0);
            mpConnectCallback = nullptr;
        }
        /* On failure: do NOT call back immediately.  The AP may deauth on the
         * first reconnect attempt (stale association state) and the WiFi stack
         * retries automatically.  Reporting failure here would abort the
         * commissioning before the retry has a chance to succeed.  Let the
         * commissioning framework's WiFiNetworkEnable timeout (35 s) act as
         * the final failure gate instead. */
    }
}

void ZephyrWifiDriver::Shutdown()
{
    mpNetworkStatusChangeCallback = nullptr;
}

CHIP_ERROR ZephyrWifiDriver::CommitConfiguration()
{
    ReturnErrorOnFailure(KeyValueStoreMgr().Put(kPassKey, mStagingNetwork.pass, mStagingNetwork.passLen));
    ReturnErrorOnFailure(KeyValueStoreMgr().Put(kSsidKey, mStagingNetwork.ssid, mStagingNetwork.ssidLen));

    return CHIP_NO_ERROR;
}

CHIP_ERROR ZephyrWifiDriver::RevertConfiguration()
{
    LoadFromStorage();

    if (WiFiManager::StationStatus::CONNECTING <= WiFiManager::Instance().GetStationStatus())
    {
        WiFiManager::WiFiInfo wifiInfo;
        ReturnErrorOnFailure(WiFiManager::Instance().GetWiFiInfo(wifiInfo));
        if (mStagingNetwork.GetSsidSpan().data_equal(ByteSpan(wifiInfo.mSsid, wifiInfo.mSsidLen)))
        {
            // we are already connected to this network, so return prematurely
            return CHIP_NO_ERROR;
        }

        WiFiManager::Instance().Disconnect();
    }

    if (mStagingNetwork.IsConfigured())
    {
        WiFiManager::ConnectionHandling handling{ [](const wifi_conn_status & connStatus) {
                                                     Instance().OnNetworkConnStatusChanged(connStatus);
                                                 },
                                                  System::Clock::Seconds32{ kWiFiConnectNetworkTimeoutSeconds } };
        ReturnErrorOnFailure(
            WiFiManager::Instance().Connect(mStagingNetwork.GetSsidSpan(), mStagingNetwork.GetPassSpan(), handling));
    }

    return CHIP_NO_ERROR;
}

Status ZephyrWifiDriver::AddOrUpdateNetwork(ByteSpan ssid, ByteSpan credentials, MutableCharSpan & outDebugText,
                                            uint8_t & outNetworkIndex)
{
    outDebugText    = {};
    outNetworkIndex = 0;

    VerifyOrReturnError(!mStagingNetwork.IsConfigured() || ssid.data_equal(mStagingNetwork.GetSsidSpan()), Status::kBoundsExceeded);
    VerifyOrReturnError(ssid.size() <= sizeof(mStagingNetwork.ssid), Status::kOutOfRange);
    VerifyOrReturnError(credentials.size() <= sizeof(mStagingNetwork.pass), Status::kOutOfRange);

    mStagingNetwork.Erase();
    memcpy(mStagingNetwork.ssid, ssid.data(), ssid.size());
    memcpy(mStagingNetwork.pass, credentials.data(), credentials.size());
    mStagingNetwork.ssidLen = ssid.size();
    mStagingNetwork.passLen = credentials.size();

    return Status::kSuccess;
}

Status ZephyrWifiDriver::RemoveNetwork(ByteSpan networkId, MutableCharSpan & outDebugText, uint8_t & outNetworkIndex)
{
    outDebugText    = {};
    outNetworkIndex = 0;

    VerifyOrReturnError(networkId.data_equal(mStagingNetwork.GetSsidSpan()), Status::kNetworkIDNotFound);
    mStagingNetwork.Clear();

    return Status::kSuccess;
}

Status ZephyrWifiDriver::ReorderNetwork(ByteSpan networkId, uint8_t index, MutableCharSpan & outDebugText)
{
    outDebugText = {};

    // Only one network is supported for now
    VerifyOrReturnError(index == 0, Status::kOutOfRange);
    VerifyOrReturnError(networkId.data_equal(mStagingNetwork.GetSsidSpan()), Status::kNetworkIDNotFound);

    return Status::kSuccess;
}

void ZephyrWifiDriver::ConnectNetwork(ByteSpan networkId, ConnectCallback * callback)
{
    Status status = Status::kSuccess;
    WiFiManager::StationStatus stationStatus;
    WiFiManager::ConnectionHandling handling{ [](const wifi_conn_status & connStatus) {
                                                 Instance().OnNetworkConnStatusChanged(connStatus);
                                             },
                                              System::Clock::Seconds32{ kWiFiConnectNetworkTimeoutSeconds } };

    /* Declare channelHint before any VerifyOrExit (which expands to goto) to
     * satisfy C++ rule that initialization may not be jumped over. */
    uint8_t channelHint = WIFI_CHANNEL_ANY;

    VerifyOrExit(mpConnectCallback == nullptr, status = Status::kUnknownError);
    mpConnectCallback = callback;

    stationStatus = WiFiManager::Instance().GetStationStatus();

    VerifyOrExit(WiFiManager::StationStatus::CONNECTING != stationStatus, status = Status::kOtherConnectionFailure);
    VerifyOrExit(networkId.data_equal(mStagingNetwork.GetSsidSpan()), status = Status::kNetworkIDNotFound);

    /* Clear NAN scan suppression so the WiFi association scan is
     * not blocked by the NAN commissioning RXP filter guard. */
#ifdef CONFIG_WIFI_NM_WPA_SUPPLICANT_NAN_USD
    qcc730_nan_glue_allow_wifi_scan();
#endif

    /* Derive the target channel from the NAN publish frequency so
     * that the internal scan is restricted to a single channel.  This avoids
     * 10-20 s full-channel scans when NAN TX repeatedly sets DISABLE0=0xffffffff
     * and keeps the WiFi connect time within the 15 s PAF ACK timer.
     *
     * NOTE: this assumes AP channel == NAN channel (current same-channel
     * constraint). Must be revisited when off-channel commissioning is supported.
     *
     * Conversion: 2.4 GHz channel = (freq_MHz - 2407) / 5
     *             5   GHz channel = (freq_MHz - 5000) / 5  (e.g. 5745 → CH149)
     */
#if CHIP_DEVICE_CONFIG_ENABLE_WIFIPAF
    {
        uint16_t apFreqMHz = chip::DeviceLayer::ConnectivityMgrImpl().WiFiPafGetApFreq();
        if (apFreqMHz >= 2412 && apFreqMHz <= 2484)
        {
            channelHint = static_cast<uint8_t>((apFreqMHz - 2407) / 5);
        }
        else if (apFreqMHz >= 5180 && apFreqMHz <= 5825)
        {
            channelHint = static_cast<uint8_t>((apFreqMHz - 5000) / 5);
        }
    }
#endif

    WiFiManager::Instance().Connect(mStagingNetwork.GetSsidSpan(), mStagingNetwork.GetPassSpan(), handling, channelHint);

exit:
    if (mpConnectCallback != nullptr)
    {
        if (status == Status::kSuccess)
        {
            if (stationStatus >= WiFiManager::StationStatus::CONNECTED)
            {
                mpConnectCallback->OnResult(status, CharSpan(), 0);
                mpConnectCallback = nullptr;
            }
        }
        else
        {
            mpConnectCallback->OnResult(status, CharSpan(), 0);
            mpConnectCallback = nullptr;
        }
    }
}

void ZephyrWifiDriver::LoadFromStorage()
{
    WiFiManager::WiFiNetwork network;

    mStagingNetwork = {};
    ReturnOnFailure(KeyValueStoreMgr().Get(kSsidKey, network.ssid, sizeof(network.ssid), &network.ssidLen));
    ReturnOnFailure(KeyValueStoreMgr().Get(kPassKey, network.pass, sizeof(network.pass), &network.passLen));
    mStagingNetwork = network;
}

void ZephyrWifiDriver::OnScanWiFiNetworkDone(const WiFiManager::ScanDoneStatus & status)
{
    VerifyOrReturn(mScanCallback != nullptr);
    mScanCallback->OnFinished(status ? Status::kUnknownError : Status::kSuccess, CharSpan(), &mScanResponseIterator);
    mScanCallback = nullptr;
}

void ZephyrWifiDriver::OnScanWiFiNetworkResult(const WiFiScanResponse & response)
{
    mScanResponseIterator.Add(response);
}

void ZephyrWifiDriver::ScanNetworks(ByteSpan ssid, WiFiDriver::ScanCallback * callback)
{
    mScanCallback    = callback;
    CHIP_ERROR error = WiFiManager::Instance().Scan(
        ssid, [](const WiFiScanResponse & response) { Instance().OnScanWiFiNetworkResult(response); },
        [](const WiFiManager::ScanDoneStatus & status) { Instance().OnScanWiFiNetworkDone(status); });

    if (error != CHIP_NO_ERROR)
    {
        mScanCallback = nullptr;
        callback->OnFinished(Status::kUnknownError, CharSpan(), nullptr);
    }
}

uint32_t ZephyrWifiDriver::GetSupportedWiFiBandsMask() const
{
    uint32_t bands = static_cast<uint32_t>(1UL << chip::to_underlying(WiFiBandEnum::k2g4));
    bands |= static_cast<uint32_t>(1UL << chip::to_underlying(WiFiBandEnum::k5g));
    return bands;
}

} // namespace NetworkCommissioning
} // namespace DeviceLayer
} // namespace chip
