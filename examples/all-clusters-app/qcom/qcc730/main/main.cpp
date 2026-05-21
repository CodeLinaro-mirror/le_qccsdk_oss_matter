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

#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <app/server/Server.h>

LOG_MODULE_REGISTER(app, CONFIG_CHIP_APP_LOG_LEVEL);
// QCC730: override Zephyr's default sys_arch_reboot() which calls NVIC_SystemReset().
// NVIC_SystemReset() only resets the Cortex-M33 core and does NOT clear RAM.
// After a software reset, rram_udpart_init() (added in qhal commit e5dd04b) calls
// qurt_mutex_create() again, but the QURT internal mutex table still holds the
// previous session's state while the BSS handle was zeroed by arch_bss_zero(),
// causing a deadlock in qapi_pmu_init() on the next boot.
// nt_system_sw_reset() writes QWLAN_PMU_SYS_SOFT_RESET_REQ_REG which resets the
// entire SoC (equivalent to a power cycle), clearing RAM and WiFi FW state cleanly.
extern "C" void nt_system_sw_reset(void);

extern "C" void sys_arch_reboot(int type)
{
    ARG_UNUSED(type);
    nt_system_sw_reset();
}

using namespace ::chip;

int main()
{
    CHIP_ERROR err = AppTask::Instance().StartApp();
    if (err != CHIP_NO_ERROR)
    {
        LOG_ERR("Exited with code %" CHIP_ERROR_FORMAT, err.Format());
        return EXIT_FAILURE;
    }
    LOG_INF("App initialized successfully");
    return EXIT_SUCCESS;
}

// Zephyr shell expects getopt_init() but we disabled Zephyr's POSIX lib ext
// to avoid getopt symbol conflicts with Newlib.
// We implement a simple getopt_init() that resets Newlib's getopt state.
#include <unistd.h>
#include <getopt.h>

extern "C" void getopt_init(void)
{
    optind = 0;
    opterr = 1;
    optopt = 0;
}

// -----------------------------------------------------------------------------
// Workaround for Zephyr wifi_shell.c linking error
// -----------------------------------------------------------------------------
// wifi_shell.c calls getopt_state_get() which is part of Zephyr's POSIX subsys.
// Since we disabled Zephyr POSIX (to use Newlib), this symbol is missing.
// We provide a shim that maps it to Newlib's global variables.


extern "C" struct getopt_state *getopt_state_get(void)
{
    static struct getopt_state s_state;
    // Sync from Newlib globals
    s_state.opterr = opterr;
    s_state.optind = optind;
    s_state.optopt = optopt;
    s_state.optarg = optarg;
    return &s_state;
}

// ---------------------------------------------------------------------------
// Zephyr shell command: "matter reset"
// Triggers a Matter factory reset — clears all NVS fabric/credential data
// and reboots. Use this when re-commissioning after a previous commissioning.
// ---------------------------------------------------------------------------
static int cmd_matter_reset(const struct shell * sh, size_t argc, char ** argv)
{
    shell_print(sh, "Matter factory reset requested — clearing NVS and rebooting...");
    chip::Server::GetInstance().ScheduleFactoryReset();
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(matter_cmds,
    SHELL_CMD(reset, NULL, "Factory reset Matter stack (clears NVS, reboots)", cmd_matter_reset),
    SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(matter, &matter_cmds, "Matter commands", NULL);
