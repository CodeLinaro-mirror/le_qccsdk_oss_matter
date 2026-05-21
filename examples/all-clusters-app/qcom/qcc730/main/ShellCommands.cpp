#include <zephyr/kernel.h>
#if CONFIG_CHIP_LIB_SHELL
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

#include "ShellCommands.h"
#include "Globals.h"
#include <lib/shell/Engine.h>
#include <lib/support/logging/CHIPLogging.h>

using namespace chip;
using namespace chip::Shell;

static CHIP_ERROR QCC730MIHelpHandler(int argc, char ** argv)
{
    streamer_printf(streamer_get(), "QCC730MI specific commands:\n");
    streamer_printf(streamer_get(), "  qcc730mi led <led_num> <on|off|blink>  : Control LEDs\n");
    streamer_printf(streamer_get(), "  qcc730mi status                        : Show device status\n");
    streamer_printf(streamer_get(), "  qcc730mi reset                         : Factory reset\n");
    return CHIP_NO_ERROR;
}

static CHIP_ERROR QCC730MILEDHandler(int argc, char ** argv)
{
    if (argc < 3)
    {
        streamer_printf(streamer_get(), "Usage: qcc730mi led <led_num> <on|off|blink>\n");
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    int ledNum = atoi(argv[1]);
    const char * action = argv[2];

    LEDWidget * led = nullptr;
    switch (ledNum)
    {
    case 1:
        led = &statusLED1;
        break;
    case 2:
        led = &statusLED2;
        break;
    case 3:
        led = &bluetoothLED;
        break;
    case 4:
        led = &pairingWindowLED;
        break;
    default:
        streamer_printf(streamer_get(), "Invalid LED number. Use 1-4\n");
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    if (strcmp(action, "on") == 0)
    {
        led->Set(true);
        streamer_printf(streamer_get(), "LED %d turned ON\n", ledNum);
    }
    else if (strcmp(action, "off") == 0)
    {
        led->Set(false);
        streamer_printf(streamer_get(), "LED %d turned OFF\n", ledNum);
    }
    else if (strcmp(action, "blink") == 0)
    {
        led->Blink(500);
        streamer_printf(streamer_get(), "LED %d blinking\n", ledNum);
    }
    else
    {
        streamer_printf(streamer_get(), "Invalid action. Use on|off|blink\n");
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    return CHIP_NO_ERROR;
}

static CHIP_ERROR QCC730MIStatusHandler(int argc, char ** argv)
{
    streamer_printf(streamer_get(), "QCC730MI Device Status:\n");
    streamer_printf(streamer_get(), "  Firmware Version: 1.0.0\n");
    streamer_printf(streamer_get(), "  Hardware Version: QCC730MI-DEV\n");
    streamer_printf(streamer_get(), "  Matter Stack: Connected Home IP\n");
    // TODO: Add more status information
    return CHIP_NO_ERROR;
}

static CHIP_ERROR QCC730MIResetHandler(int argc, char ** argv)
{
    streamer_printf(streamer_get(), "Factory reset requested...\n");
    // TODO: Implement factory reset functionality
    return CHIP_NO_ERROR;
}

static CHIP_ERROR QCC730MIDispatch(int argc, char ** argv)
{
    if (argc == 0)
    {
        return QCC730MIHelpHandler(argc, argv);
    }

    if (strcmp(argv[0], "help") == 0)
    {
        return QCC730MIHelpHandler(argc - 1, argv + 1);
    }
    else if (strcmp(argv[0], "led") == 0)
    {
        return QCC730MILEDHandler(argc - 1, argv + 1);
    }
    else if (strcmp(argv[0], "status") == 0)
    {
        return QCC730MIStatusHandler(argc - 1, argv + 1);
    }
    else if (strcmp(argv[0], "reset") == 0)
    {
        return QCC730MIResetHandler(argc - 1, argv + 1);
    }
    else
    {
        return QCC730MIHelpHandler(argc, argv);
    }
}

void // RegisterQCC730MICommands()
{
    static const shell_command_t sQCC730MICommand = { &QCC730MIDispatch, "qcc730mi", "QCC730MI specific commands" };
    Engine::Root().RegisterCommands(&sQCC730MICommand, 1);
}

#endif // CONFIG_CHIP_LIB_SHELL
