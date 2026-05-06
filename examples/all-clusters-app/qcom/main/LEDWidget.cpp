/*
 *
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

#include "LEDWidget.h"
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>

#include <stdio.h>
#include <sys/time.h>

using namespace chip;
using namespace chip::DeviceLayer;

void LEDWidget::Init(uint8_t gpioNum)
{
    mLastChangeTimeMS = 0;
    mBlinkOnTimeMS    = 0;
    mBlinkOffTimeMS   = 0;
    mGPIONum          = gpioNum;
    mState            = false;
    mBlinkState       = false;

    // TODO: Initialize GPIO for QCC730MI
    ChipLogProgress(NotSpecified, "LED Widget initialized on GPIO %d", gpioNum);
}

void LEDWidget::Set(bool state)
{
    mBlinkOnTimeMS = mBlinkOffTimeMS = 0;
    DoSet(state);
}

void LEDWidget::Blink(uint32_t changeRateMS)
{
    Blink(changeRateMS, changeRateMS);
}

void LEDWidget::Blink(uint32_t onTimeMS, uint32_t offTimeMS)
{
    mBlinkOnTimeMS  = onTimeMS;
    mBlinkOffTimeMS = offTimeMS;
    Animate();
}

void LEDWidget::Animate()
{
    if (mBlinkOnTimeMS != 0 && mBlinkOffTimeMS != 0)
    {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        uint64_t nowMS = (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
        uint64_t stateDurMS = mBlinkState ? mBlinkOnTimeMS : mBlinkOffTimeMS;
        
        if (nowMS > mLastChangeTimeMS + stateDurMS)
        {
            DoSet(!mBlinkState);
            mBlinkState = !mBlinkState;
            mLastChangeTimeMS = nowMS;
        }
    }
}

void LEDWidget::DoSet(bool state)
{
    mState = state;
    
    // TODO: Implement actual GPIO control for QCC730MI
    // For now, just log the state change
    ChipLogProgress(NotSpecified, "LED GPIO %d set to %s", mGPIONum, state ? "ON" : "OFF");
    
    // Example QCC730MI GPIO control would go here:
    // qcc730mi_gpio_set_level(mGPIONum, state ? 1 : 0);
}
