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

#include "DeviceCallbacks.h"
#include "Globals.h"
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <lib/support/logging/CHIPLogging.h>

using namespace chip;
using namespace chip::DeviceLayer;
using namespace chip::app::Clusters;

void AppDeviceCallbacks::OnInternetConnectivityChange(const ChipDeviceEvent * event)
{
    if (event->InternetConnectivityChange.IPv4 == kConnectivity_Established)
    {
        ChipLogProgress(NotSpecified, "IPv4 Server ready...");
        statusLED1.Set(true);
    }
    else if (event->InternetConnectivityChange.IPv4 == kConnectivity_Lost)
    {
        ChipLogProgress(NotSpecified, "Lost IPv4 connectivity...");
        statusLED1.Set(false);
    }
    if (event->InternetConnectivityChange.IPv6 == kConnectivity_Established)
    {
        ChipLogProgress(NotSpecified, "IPv6 Server ready...");
        statusLED2.Set(true);
    }
    else if (event->InternetConnectivityChange.IPv6 == kConnectivity_Lost)
    {
        ChipLogProgress(NotSpecified, "Lost IPv6 connectivity...");
        statusLED2.Set(false);
    }
}

void AppDeviceCallbacks::OnOnOffPostAttributeChangeCallback(EndpointId endpointId, AttributeId attributeId, uint8_t * value)
{
    if (attributeId != OnOff::Attributes::OnOff::Id)
    {
        ChipLogError(DeviceLayer, "Unhandled Attribute ID: '0x%04x", attributeId);
        return;
    }
    if (endpointId != 1)
    {
        ChipLogError(DeviceLayer, "Unexpected EndPoint ID: `0x%02x'", endpointId);
        return;
    }
    ChipLogProgress(NotSpecified, "OnOff attribute changed to %s", *value ? "ON" : "OFF");
}

void AppDeviceCallbacks::OnIdentifyPostAttributeChangeCallback(EndpointId endpointId, AttributeId attributeId, uint8_t * value)
{
    if (attributeId != Identify::Attributes::IdentifyTime::Id)
    {
        ChipLogError(DeviceLayer, "Unhandled Attribute ID: '0x%04x", attributeId);
        return;
    }
    if (endpointId != 1)
    {
        ChipLogError(DeviceLayer, "Unexpected EndPoint ID: `0x%02x'", endpointId);
        return;
    }
    uint16_t identifyTime = *reinterpret_cast<uint16_t *>(value);
    ChipLogProgress(NotSpecified, "Identify time changed to %d seconds", identifyTime);
    if (identifyTime > 0)
    {
        statusLED1.Blink(500);
    }
    else
    {
        statusLED1.Set(false);
    }
}

void AppDeviceCallbacks::OnLevelControlAttributeChangeCallback(EndpointId endpointId, AttributeId attributeId, uint8_t * value)
{
    if (attributeId != LevelControl::Attributes::CurrentLevel::Id)
    {
        ChipLogError(DeviceLayer, "Unhandled Attribute ID: '0x%04x", attributeId);
        return;
    }
    if (endpointId != 1)
    {
        ChipLogError(DeviceLayer, "Unexpected EndPoint ID: `0x%02x'", endpointId);
        return;
    }
    uint8_t level = *value;
    ChipLogProgress(NotSpecified, "Level control changed to %d", level);
}

void AppDeviceCallbacks::OnColorControlAttributeChangeCallback(EndpointId endpointId, AttributeId attributeId, uint8_t * value)
{
    if (endpointId != 1)
    {
        ChipLogError(DeviceLayer, "Unexpected EndPoint ID: `0x%02x'", endpointId);
        return;
    }
    ChipLogProgress(NotSpecified, "Color control attribute 0x%04x changed", attributeId);
}
