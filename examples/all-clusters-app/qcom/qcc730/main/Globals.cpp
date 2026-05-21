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

#include "Globals.h"

// QCC730MI LED GPIO pin definitions
// TODO: Update these with actual QCC730MI GPIO pin numbers
#define STATUS_LED_GPIO_1 2
#define STATUS_LED_GPIO_2 3
#define BLUETOOTH_LED_GPIO 4
#define PAIRING_WINDOW_LED_GPIO 5

// Global LED instances
LEDWidget statusLED1;
LEDWidget statusLED2;
LEDWidget bluetoothLED;
LEDWidget pairingWindowLED;

// Initialize all LEDs
void InitializeGlobalLEDs()
{
    statusLED1.Init(STATUS_LED_GPIO_1);
    statusLED2.Init(STATUS_LED_GPIO_2);
    bluetoothLED.Init(BLUETOOTH_LED_GPIO);
    pairingWindowLED.Init(PAIRING_WINDOW_LED_GPIO);
}
