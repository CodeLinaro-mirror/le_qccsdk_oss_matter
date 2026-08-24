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
#include "lock_fw.h"
#include "QcomLockManager.h"

#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/pm/policy.h>
#include <app/server/Server.h>
#include <app/clusters/door-lock-server/door-lock-server.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <platform/CHIPDeviceLayer.h>

LOG_MODULE_REGISTER(app, CONFIG_CHIP_APP_LOG_LEVEL);

// QCC730: override sys_arch_reboot() to use full SoC reset.
extern "C" void nt_system_sw_reset(void);

extern "C" void sys_arch_reboot(int type)
{
    ARG_UNUSED(type);
    nt_system_sw_reset();
}

using namespace ::chip;
using namespace chip::app::Clusters::DoorLock;
using chip::app::DataModel::Nullable;

static constexpr chip::EndpointId kLockEndpointId = 1;

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

#include <unistd.h>
#include <getopt.h>

extern "C" void getopt_init(void)
{
    optind = 0;
    opterr = 1;
    optopt = 0;
}

extern "C" struct getopt_state *getopt_state_get(void)
{
    static struct getopt_state s_state;
    s_state.opterr = opterr;
    s_state.optind = optind;
    s_state.optopt = optopt;
    s_state.optarg = optarg;
    return &s_state;
}

// ---------------------------------------------------------------------------
// Zephyr shell command: "matter reset"
// ---------------------------------------------------------------------------
static int cmd_matter_reset(const struct shell * sh, size_t argc, char ** argv)
{
    shell_print(sh, "Matter factory reset requested — clearing NVS and rebooting...");
    chip::Server::GetInstance().ScheduleFactoryReset();
    return 0;
}

#if CONFIG_LOCK_HOST_MODE
static int cmd_matter_sleep(const struct shell * sh, size_t argc, char ** argv)
{
    if (pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_RAM, PM_ALL_SUBSTATES))
    {
        pm_policy_state_lock_put(PM_STATE_SUSPEND_TO_RAM, PM_ALL_SUBSTATES);
        shell_print(sh, "boot S2RAM lock released — ICD sleep enabled");
    }
    else
    {
        shell_print(sh, "S2RAM lock already released (cnt=0)");
    }
    return 0;
}
#endif /* CONFIG_LOCK_HOST_MODE */

#if !CONFIG_LOCK_HOST_MODE
struct PinCmdCtx {
    char pin[32];
    uint8_t pinLen;
};

static void PinUnlockOnMatterThread(intptr_t context)
{
    auto * ctx = reinterpret_cast<PinCmdCtx *>(context);

    Optional<ByteSpan> pin = MakeOptional(ByteSpan(
        reinterpret_cast<const uint8_t *>(ctx->pin), ctx->pinLen));

    OperationErrorEnum err = OperationErrorEnum::kUnspecified;
    bool valid = QcomLockMgr().ValidatePIN(kLockEndpointId, pin, err);

    if (!valid) {
        printk("[PIN] invalid PIN — err=%d\n", to_underlying(err));
    } else {
        printk("[PIN] valid — unlocking\n");
        DoorLockServer::Instance().SetLockState(
            kLockEndpointId, DlLockState::kUnlocked,
            OperationSourceEnum::kKeypad,
            NullNullable, NullNullable,
            NullNullable, NullNullable);
    }

    chip::Platform::Delete(ctx);
}

static int cmd_matter_pin(const struct shell * sh, size_t argc, char ** argv)
{
    if (argc < 2) {
        shell_print(sh, "Usage: matter pin <code>");
        return -EINVAL;
    }

    auto * ctx = chip::Platform::New<PinCmdCtx>();
    if (!ctx) {
        shell_print(sh, "OOM");
        return -ENOMEM;
    }

    size_t len = strlen(argv[1]);
    if (len > sizeof(ctx->pin) - 1)
        len = sizeof(ctx->pin) - 1;
    memcpy(ctx->pin, argv[1], len);
    ctx->pinLen = static_cast<uint8_t>(len);

    chip::DeviceLayer::PlatformMgr().ScheduleWork(
        PinUnlockOnMatterThread, reinterpret_cast<intptr_t>(ctx));

    shell_print(sh, "PIN submitted");
    return 0;
}
#endif /* !CONFIG_LOCK_HOST_MODE */

SHELL_STATIC_SUBCMD_SET_CREATE(matter_cmds,
    SHELL_CMD(reset, NULL, "Factory reset Matter stack (clears NVS, reboots)", cmd_matter_reset),
#if CONFIG_LOCK_HOST_MODE
    SHELL_CMD(sleep, NULL, "Release boot S2RAM lock to enable ICD sleep", cmd_matter_sleep),
#endif
#if !CONFIG_LOCK_HOST_MODE
    SHELL_CMD_ARG(pin, NULL, "Unlock with PIN code: matter pin <code>", cmd_matter_pin, 2, 0),
#endif
    SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(matter, &matter_cmds, "Matter commands", NULL);
