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

#include <app/server/Server.h>

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

void AppTask::ButtonEventHandler(uint8_t btnIdx, uint8_t btnAction)
{
    AppEvent button_event             = {};
    button_event.mType                = AppEvent::kEventType_Button;
    button_event.mButtonEvent.mPinNo  = btnIdx;
    button_event.mButtonEvent.mAction = btnAction;
    button_event.mHandler             = ButtonPressedAction;

    PostEvent(&button_event);
}

void AppTask::ButtonPressedAction(AppEvent * aEvent)
{
    uint32_t io_num = aEvent->mButtonEvent.mPinNo;
    uint32_t level  = aEvent->mButtonEvent.mAction;

    ChipLogProgress(NotSpecified, "Button %d pressed, level: %d", io_num, level);

    if (level == 0)
    {
        Instance().mFunction = kFunction_StartBleAdv;
        Instance().StartTimer(1000);
    }
}

CHIP_ERROR AppTask::Init()
{
    ChipLogProgress(NotSpecified, "AppTask::Init()");

    mFunction               = kFunction_NoneSelected;
    mFunctionTimerActive    = false;
    mSyncClusterToButtonAction = false;
	
	// chip::app::Clusters::InitializeTlsClientManagement();
    // chip::app::Clusters::InitializeTlsCertificateManagement();
    // Initialize the CHIP stack.
    CHIP_ERROR err = PlatformMgr().InitChipStack();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "PlatformMgr().InitChipStack() failed: %s", ErrorStr(err));
        return err;
    }

    // Set device attestation credentials.
    SetDeviceAttestationCredentialsProvider(Credentials::Examples::GetExampleDACProvider());

    // Initialize the Matter server (commissioning, clusters, mDNS).
    // Register the WiFi NetworkCommissioning driver before Server::Init().
    // Platform::MemoryInit() must be called before any chip::Platform::New/MemoryAlloc
    // (e.g. AddEventHandler inside NetworkCommissioning::Instance::Init()).
    // Server::Init() also calls it, but we need it earlier.
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

    PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kOnNetwork));

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
