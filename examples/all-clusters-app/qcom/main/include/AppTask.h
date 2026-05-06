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

#include "AppEvent.h"

#include <platform/CHIPDeviceLayer.h>

#include <zephyr/kernel.h>

// Application-defined error codes in the CHIP_ERROR space.
#define APP_ERROR_EVENT_QUEUE_FAILED  CHIP_APPLICATION_ERROR(0x01)
#define APP_ERROR_CREATE_TASK_FAILED  CHIP_APPLICATION_ERROR(0x02)
#define APP_ERROR_UNHANDLED_EVENT     CHIP_APPLICATION_ERROR(0x03)
#define APP_ERROR_CREATE_TIMER_FAILED CHIP_APPLICATION_ERROR(0x04)
#define APP_ERROR_START_TIMER_FAILED  CHIP_APPLICATION_ERROR(0x05)
#define APP_ERROR_STOP_TIMER_FAILED   CHIP_APPLICATION_ERROR(0x06)
#define APP_ERROR_ALLOCATION_FAILED   CHIP_APPLICATION_ERROR(0x07)

class AppTask
{
public:
    static AppTask & Instance()
    {
        static AppTask sAppTask;
        return sAppTask;
    }

    CHIP_ERROR StartApp();
    void PostEvent(const AppEvent * event);
    void ButtonEventHandler(uint8_t btnIdx, uint8_t btnAction);

private:
    CHIP_ERROR Init();

    void StartTimer(uint32_t aTimeoutMs);
    void CancelTimer();

    static void AppTaskMain(void * p1, void * p2, void * p3);
    static void ButtonPressedAction(AppEvent * aEvent);
    static void FunctionTimerEventHandler(AppEvent * aEvent);
    void DispatchEvent(AppEvent * event);

    enum Function_t
    {
        kFunction_NoneSelected   = 0,
        kFunction_SoftwareUpdate = 0,
        kFunction_StartBleAdv    = 1,
        kFunction_FactoryReset   = 2,
        kFunction_Invalid
    };

    Function_t mFunction            = kFunction_NoneSelected;
    bool mFunctionTimerActive       = false;
    bool mSyncClusterToButtonAction = false;

    k_tid_t mAppTaskHandle;
};
