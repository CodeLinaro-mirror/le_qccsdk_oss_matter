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

#include "AppTask.h"
#include "AppEvent.h"
#include "LockManager.h"
#include "Identify.h"
#include "lock_fw.h"

#include <app/server/Server.h>
#include <zephyr/pm/policy.h>
extern "C" {
#include <qcom_wifi_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
}

#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/PlatformManager.h>
#include <data-model-providers/codegen/Instance.h>
#include <app/clusters/network-commissioning/network-commissioning.h>
#include <platform/Zephyr/wifi/ZephyrWifiDriver.h>
#include <lib/support/CHIPMem.h>
#include <setup_payload/OnboardingCodesUtil.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace chip;
using namespace chip::DeviceLayer;

// NetworkCommissioning WiFi instance — wires the QCC730 WiFi driver to the
// NetworkCommissioning cluster on endpoint 0.
app::Clusters::NetworkCommissioning::Instance
    sWiFiNetworkCommissioningInstance(0 /* endpoint */, &NetworkCommissioning::ZephyrWifiDriver::Instance());

namespace {

constexpr size_t kAppTaskStackSize = 4096;
constexpr int    kAppTaskPriority  = 5;

// Lock endpoint configuration
constexpr chip::EndpointId kLockEndpointId = 1;

K_THREAD_STACK_DEFINE(sAppTaskStack, kAppTaskStackSize);
struct k_thread sAppTaskData;

K_MSGQ_DEFINE(sAppEventQueue, sizeof(AppEvent), 10, alignof(AppEvent));

} // namespace

CHIP_ERROR AppTask::StartApp()
{
    CHIP_ERROR err = Init();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "AppTask::Init() failed: %s", ErrorStr(err));
        return err;
    }

    mAppTaskHandle = k_thread_create(&sAppTaskData, sAppTaskStack,
                                     K_THREAD_STACK_SIZEOF(sAppTaskStack),
                                     AppTaskMain, nullptr, nullptr, nullptr,
                                     kAppTaskPriority, 0, K_NO_WAIT);
    k_thread_name_set(mAppTaskHandle, "APP");

    return CHIP_NO_ERROR;
}

void AppTask::AppTaskMain(void * p1, void * p2, void * p3)
{
    AppEvent event;

    ChipLogProgress(NotSpecified, "App Task started");

    while (true)
    {
        int ret = k_msgq_get(&sAppEventQueue, &event, K_FOREVER);
        if (ret == 0)
        {
            Instance().DispatchEvent(&event);
        }
    }
}

void AppTask::PostEvent(const AppEvent * aEvent)
{
    if (k_msgq_put(&sAppEventQueue, aEvent, K_NO_WAIT) != 0)
    {
        ChipLogError(NotSpecified, "Failed to post event to app task event queue");
    }
}

CHIP_ERROR AppTask::Init()
{
    ChipLogProgress(NotSpecified, "AppTask::Init()");

    mFunction               = kFunction_NoneSelected;
    mFunctionTimerActive    = false;
    mSyncClusterToButtonAction = false;

    // Initialize the CHIP stack.
    CHIP_ERROR err = PlatformMgr().InitChipStack();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "PlatformMgr().InitChipStack() failed: %s", ErrorStr(err));
        return err;
    }

    // Set device attestation credentials.
    SetDeviceAttestationCredentialsProvider(Credentials::Examples::GetExampleDACProvider());

    chip::Platform::MemoryInit();

    err = sWiFiNetworkCommissioningInstance.Init();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "NetworkCommissioning::Instance::Init() failed: %s", ErrorStr(err));
        return err;
    }

    static chip::CommonCaseDeviceServerInitParams serverInitParams;
    (void) serverInitParams.InitializeStaticResourcesBeforeServerInit();
    serverInitParams.dataModelProvider = chip::app::CodegenDataModelProviderInstance(
        serverInitParams.persistentStorageDelegate);
    err = chip::Server::GetInstance().Init(serverInitParams);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "Server::Init() failed: %s", ErrorStr(err));
        return err;
    }

    /* Register ICD observer — AppTask itself implements ICDStateObserver */
#if CHIP_CONFIG_ENABLE_ICD_SERVER
    auto * icdObserver = chip::Server::GetInstance().GetICDManager().RegisterObserver(this);
    if (icdObserver == nullptr)
    {
        ChipLogError(NotSpecified, "ICDManager::RegisterObserver failed — pool full! Increase CHIP_CONFIG_ICD_OBSERVERS_POOL_SIZE");
        return CHIP_ERROR_NO_MEMORY;
    }
#endif

    // Initialize the LockManager with the Door Lock cluster endpoint.
    if (!LockManager::Instance().InitEndpoint(kLockEndpointId))
    {
        ChipLogError(NotSpecified, "LockManager::InitEndpoint() failed");
        return CHIP_ERROR_INTERNAL;
    }

#if CONFIG_LOCK_HOST_MODE
    // Initialize ring service FW side (register RX callback for Host messages).
    // init_qring() is called automatically at boot via SYS_INIT(POST_KERNEL, 50).
    err = lock_fw_init(kLockEndpointId);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "lock_fw_init() failed: %s", ErrorStr(err));
        return err;
    }
#endif /* CONFIG_LOCK_HOST_MODE */

    // Initialize the Identify cluster delegate.
    IdentifyInit();

    PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kOnNetwork));

    PlatformMgr().AddEventHandler(OnPlatformEvent, reinterpret_cast<intptr_t>(this));

    // Start the CHIP event loop.
    err = PlatformMgr().StartEventLoopTask();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "PlatformMgr().StartEventLoopTask() failed: %s", ErrorStr(err));
        return err;
    }

    return CHIP_NO_ERROR;
}

void AppTask::StartTimer(uint32_t aTimeoutMs)
{
    ChipLogProgress(NotSpecified, "Timer started for %u ms", aTimeoutMs);
    mFunctionTimerActive = true;
}

void AppTask::CancelTimer()
{
    ChipLogProgress(NotSpecified, "Timer cancelled");
    mFunctionTimerActive = false;
}

void AppTask::FunctionTimerEventHandler(AppEvent * aEvent)
{
    AppTask & task = Instance();
    if (task.mFunctionTimerActive)
    {
        task.CancelTimer();

        switch (task.mFunction)
        {
        case kFunction_StartBleAdv:
            ChipLogProgress(NotSpecified, "Starting BLE advertisement");
            break;
        case kFunction_FactoryReset:
            ChipLogProgress(NotSpecified, "Factory reset requested");
            break;
        default:
            break;
        }
    }
}

void AppTask::DispatchEvent(AppEvent * aEvent)
{
    if (aEvent->mHandler)
    {
        aEvent->mHandler(aEvent);
    }
    else
    {
        ChipLogError(NotSpecified, "Event received with no handler. Dropping event.");
    }
}

/* =========================================================
 * Platform event handler
 * ========================================================= */

void AppTask::OnPlatformEvent(const chip::DeviceLayer::ChipDeviceEvent * event,
                               intptr_t arg)
{
    AppTask * self = reinterpret_cast<AppTask *>(arg);

    if (event->Type == chip::DeviceLayer::DeviceEventType::kWiFiConnectivityChange)
    {
        if (event->WiFiConnectivityChange.Result == kConnectivity_Established)
        {
            if (!self->mBootLockReleased)
            {
                self->mBootLockReleased = true;
                if (pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_RAM, PM_ALL_SUBSTATES))
                {
                    pm_policy_state_lock_put(PM_STATE_SUSPEND_TO_RAM, PM_ALL_SUBSTATES);
                }
                printk("[APP] WiFi connected — boot S2RAM lock released\n");
            }
        }
    }
}

/* =========================================================
 * ICD State Observer callbacks
 *
 * OnEnterActiveMode: BMPS off + lock S2RAM (MCU must stay awake)
 * OnEnterIdleMode:   BMPS on  + unlock S2RAM (WiFi FW handles beacons)
 *
 * BMPS is only enabled when WiFi is already connected.
 * If WiFi is not up yet, skip BMPS (ICD Manager starts at boot
 * before WiFi connects; BMPS will be enabled on the next Idle cycle).
 * ========================================================= */

#if CHIP_CONFIG_ENABLE_ICD_SERVER
void AppTask::OnEnterActiveMode()
{
    printk("[APP] ICD: OnEnterActiveMode\n");

    /* Lock S2RAM: prevent deep sleep during active window */
    pm_policy_state_lock_get(PM_STATE_SUSPEND_TO_RAM, PM_ALL_SUBSTATES);

    /* Disable BMPS */
    struct net_if * iface = net_if_get_wifi_sta();
    if (iface)
    {
        struct qcom_wifi_pm_bmps_params bmps = { .enable = 0 };
        int err = net_mgmt(NET_REQUEST_WIFI_PM_QCOM_SET_BMPS_ENABLE, iface, &bmps, sizeof(bmps));
        printk("[APP] ICD: BMPS disable err=%d\n", err);
    }
}

void AppTask::OnEnterIdleMode()
{
    printk("[APP] ICD: OnEnterIdleMode\n");

    struct net_if * iface = net_if_get_wifi_sta();
    bool wifi_up = iface && net_if_is_up(iface);

    if (wifi_up)
    {
        /* Enable BMPS */
        struct qcom_wifi_pm_bmps_params bmps = { .enable = 1 };
        int err = net_mgmt(NET_REQUEST_WIFI_PM_QCOM_SET_BMPS_ENABLE, iface, &bmps, sizeof(bmps));
        (void)err;
    }
    else
    {
        printk("[APP] ICD: WiFi not up, skip BMPS\n");
    }

    /* Release the OnEnterActiveMode lock to keep cnt balanced. */
    bool s2ram_locked = pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_RAM, PM_ALL_SUBSTATES);
    if (s2ram_locked)
    {
        pm_policy_state_lock_put(PM_STATE_SUSPEND_TO_RAM, PM_ALL_SUBSTATES);
    }
}
#endif // CHIP_CONFIG_ENABLE_ICD_SERVER
