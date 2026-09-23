/*
 * lock_fw.cpp — QCC730 FW side ring service handler for lock app
 *
 * Design:
 *   lock_fw_send_cmd() sends a command to Host and stores the operation
 *   context. State is updated ONLY after the corresponding RSP arrives:
 *
 *     Send cmd → gPendingOp.pending=true + start timeout timer
 *
 *     Case A: RSP arrives (ring callback thread)
 *       → ScheduleWork → CHIP thread
 *           → CancelTimer + SetLockState(targetState) + pending=false
 *
 *     Case B: Timeout (CHIP thread, SystemLayer timer)
 *       → pending=false (no state update, hardware unresponsive)
 *
 *   gPendingOp.pending also serves as a concurrent-operation guard:
 *   a new request is rejected while one is pending.
 *
 * Thread safety:
 *   gPendingOp is written from CHIP thread (send/complete/timeout)
 *   and read from ring callback thread (RSP handler).
 *   On Cortex-M33, bool writes are atomic; we set pending=true LAST
 *   (after all fields are populated) and clear it FIRST (before any
 *   further processing).
 */

#include "lock_fw.h"
#include "QcomLockManager.h"

#include <app/clusters/door-lock-server/door-lock-server.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/PlatformManager.h>
#include <platform/CHIPDeviceLayer.h>
#include <lib/support/CHIPMem.h>
#include <app/server/Server.h>
#if CHIP_CONFIG_ENABLE_ICD_SERVER
#include <app/icd/server/ICDNotifier.h>
#endif

#include <zephyr/logging/log.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/policy.h>
#include <zephyr/pm/device.h>
#include <errno.h>

extern "C" {
#if CONFIG_LOCK_HOST_MODE
#include "ring_service.h"
/* qcspi driver wakeup flag API */
bool spi_is_ext_wakeup(void);
void spi_set_ext_wakeup_flag(void);
void spi_clear_ext_wakeup_flag(void);
#endif /* CONFIG_LOCK_HOST_MODE */
}

LOG_MODULE_REGISTER(lock_fw, LOG_LEVEL_INF);

using namespace chip;
using namespace chip::app::Clusters::DoorLock;
using chip::app::DataModel::Nullable;

/* Module state */
static EndpointId   s_endpoint = 1;

static uint8_t      s_rx_buf[64];

/* =========================================================
 * Pending operation context
 * ========================================================= */

struct PendingOp {
    bool           pending = false;
    uint8_t        cmd     = 0;
    LockCmdCtx ctx;
};

/* Context for CRED_IND deferred processing on CHIP thread */
struct CredIndCtx {
    OperationSourceEnum opSource;
    uint8_t             pinLen;
    uint8_t             pinData[32];
};

static PendingOp    gPendingOp;

/* =========================================================
 * CRED_IND handler — runs on CHIP thread (host mode only)
 * ========================================================= */
static void HandleCredIndOnMatterThread(intptr_t context)
{
    auto *ctx = reinterpret_cast<CredIndCtx *>(context);
    OperationErrorEnum err = OperationErrorEnum::kUnspecified;
    bool credValid = false;

    if (ctx->opSource == OperationSourceEnum::kBiometric)
    {
        /* Biometric: credData[0..1] = PageID (big-endian) from AS608.
         * Host already identified the finger; FW verifies the credential
         * is registered in the Matter credential table. */
        if (ctx->pinLen >= 2)
        {
            uint16_t credentialIndex = ((uint16_t)ctx->pinData[0] << 8) | ctx->pinData[1];
            credValid = QcomLockMgr().ValidateBiometric(s_endpoint, credentialIndex, err);
        }
    }
    else  /* kKeypad */
    {
        Optional<ByteSpan> pin = MakeOptional(ByteSpan(ctx->pinData, ctx->pinLen));
        credValid = QcomLockMgr().ValidatePIN(s_endpoint, pin, err);
    }

    /* Send CRED_ACK with result.
     * lock_fw_send_ack() clears spi_clear_ext_wakeup_flag() internally.
     * Host sends SLEEP_OK after CRED_ACK regardless of result. */
    lock_fw_send_ack(LOCK_MSG_CRED_ACK,
                     credValid ? LOCK_RESULT_OK : LOCK_RESULT_ERR,
                     to_underlying(ctx->opSource));

    if (credValid)
    {
        /* S2RAM lock is still held (SLEEP_OK not yet received).
         * Send UNLOCK_CMD; Host will execute hw_unlock + send UNLOCK_ACK.
         * pm_policy_state_lock is released when SLEEP_OK arrives. */
        int ret = QcomLockMgr().SendUnlockCmd(s_endpoint, ctx->opSource);
        if (ret != true) {
            ChipLogError(NotSpecified, "lock_fw: UNLOCK_CMD send failed after local auth");
        }
    }
    else
    {
        ChipLogError(NotSpecified, "lock_fw: local auth failed (src=0x%02x err=%d)",
                     to_underlying(ctx->opSource), to_underlying(err));
        /* pm_policy_state_lock will be released when Host sends SLEEP_OK */
    }

    Platform::Delete(ctx);
}

/* =========================================================
 * Complete operation on CHIP thread (called from ScheduleWork or timeout)
 * ========================================================= */

struct CompleteCtx {
    uint8_t        cmd;
    bool           success;
    LockCmdCtx ctx;
};

static void OnRspTimeout(System::Layer *, void *);

static void CompleteOnMatterThread(intptr_t context)
{
    auto *ctx = reinterpret_cast<CompleteCtx *>(context);

    chip::DeviceLayer::SystemLayer().CancelTimer(OnRspTimeout, nullptr);
    gPendingOp.pending = false;

    /* LOCK_CMD / UNLOCK_CMD / UNBOLT_CMD */
    if (ctx->success) {
        ChipLogProgress(NotSpecified, "lock_fw: ACK OK → SetLockState");
        DoorLockServer::Instance().SetLockState(
            ctx->ctx.endpoint, ctx->ctx.bolt.targetState, ctx->ctx.bolt.opSource,
            NullNullable, NullNullable, ctx->ctx.fabricIdx, ctx->ctx.nodeId);
    } else {
        ChipLogError(NotSpecified, "lock_fw: ACK ERR — hardware failed, state not updated");
    }
    QcomLockMgr().OnOperationCompleted(ctx->success);

    Platform::Delete(ctx);
}

/* =========================================================
 * Safety timeout handler — runs on CHIP thread
 * ========================================================= */

static void OnRspTimeout(System::Layer *, void *)
{
    ChipLogError(NotSpecified, "lock_fw: RSP timeout for cmd=0x%02x — no response from Host",
                 gPendingOp.cmd);
    gPendingOp.pending = false;
    /* State is NOT updated — hardware status unknown */
}

/* =========================================================
 * Ring RX callback — runs in ring work queue thread
 * RSP matching: check pending flag AND that cmd type matches RSP type.
 * ========================================================= */

static void ScheduleComplete(bool success)
{
    auto *ctx = Platform::New<CompleteCtx>();
    if (!ctx) {
        LOG_ERR("OOM scheduling complete");
        gPendingOp.pending = false;
        return;
    }
    ctx->cmd     = gPendingOp.cmd;
    ctx->success = success;
    ctx->ctx     = gPendingOp.ctx;   /* copy entire context block */

    DeviceLayer::PlatformMgr().ScheduleWork(CompleteOnMatterThread,
                                             reinterpret_cast<intptr_t>(ctx));
}

/* =========================================================
 * Ring TX helpers
 * ========================================================= */

int lock_fw_send_ack(uint8_t type, uint8_t result, uint8_t opSource)
{
    /* Clear ext_wakeup_flag before ring_send so that the HOST_INT1 raised
     * when Host reads this ACK can call pm_device_busy_clear normally. */
    spi_clear_ext_wakeup_flag();
    uint8_t msg[3] = { type, result, opSource };
    int ret = ring_send(LOCK_FW_RING_ID, msg, sizeof(msg), K_MSEC(100));
    if (ret != 0) {
        LOG_ERR("lock_fw_send_ack type=0x%02x failed: %d", type, ret);
    } else {
        LOG_INF("CRED_ACK sent: type=0x%02x result=%d opSource=0x%02x", type, result, opSource);
    }
    return ret;
}

/* =========================================================
 * Ring RX handler helpers (type-based dispatch)
 * Only compiled in Host mode.
 * ========================================================= */
static void handle_host_ack(uint8_t type, uint8_t result)
{
    switch (type) {
    case LOCK_MSG_LOCK_ACK:
        LOG_INF("LOCK_ACK result=%d", result);
        if (gPendingOp.pending && gPendingOp.cmd == LOCK_MSG_LOCK_CMD) {
            ScheduleComplete(result == LOCK_RESULT_OK);
        } else {
            LOG_WRN("LOCK_ACK ignored (pending=%d cmd=0x%02x)",
                    gPendingOp.pending, gPendingOp.cmd);
        }
        break;

    case LOCK_MSG_UNLOCK_ACK:
        LOG_INF("UNLOCK_ACK result=%d", result);
        if (gPendingOp.pending &&
            (gPendingOp.cmd == LOCK_MSG_UNLOCK_CMD ||
             gPendingOp.cmd == LOCK_MSG_UNBOLT_CMD)) {
            ScheduleComplete(result == LOCK_RESULT_OK);
        } else {
            LOG_WRN("UNLOCK_ACK ignored (pending=%d cmd=0x%02x)",
                    gPendingOp.pending, gPendingOp.cmd);
        }
        break;

    case LOCK_MSG_ENROLL_ACK:
        /* Immediate ACK from Host — enrollment started, not yet complete.
         * Clear pending so next command can proceed; ENROLL_IND will arrive later. */
        LOG_INF("ENROLL_ACK — enrollment started on Host");
        chip::DeviceLayer::SystemLayer().CancelTimer(OnRspTimeout, nullptr);
        gPendingOp.pending = false;
        break;

    default:
        LOG_WRN("Unknown ACK type: 0x%02x result=%d", type, result);
        break;
    }
}

static void handle_host_ind(uint8_t type, const uint8_t *buf, int len)
{
    uint8_t payload1 = (len > 1) ? buf[1] : 0;

    switch (type) {
    case LOCK_MSG_CRED_IND: {
        /* Format: [0x81][OperationSourceEnum:1B][cred_len:1B][cred_data...] */
        if (len < 3) { LOG_WRN("CRED_IND too short"); break; }

        auto opSource = static_cast<OperationSourceEnum>(buf[1]);
        uint8_t credLen = buf[2];

        if (credLen == 0 || credLen > 32 || len < 3 + credLen)
        {
            LOG_WRN("CRED_IND invalid length (credLen=%d)", credLen);
            break;
        }

        if (opSource != OperationSourceEnum::kKeypad &&
            opSource != OperationSourceEnum::kBiometric)
        {
            LOG_INF("CRED_IND src=0x%02x not supported", to_underlying(opSource));
            break;
        }

        auto *ctx = Platform::New<CredIndCtx>();
        if (!ctx) { LOG_ERR("OOM for CredIndCtx"); break; }

        ctx->opSource = opSource;
        ctx->pinLen   = credLen;
        memcpy(ctx->pinData, &buf[3], credLen);

        DeviceLayer::PlatformMgr().ScheduleWork(HandleCredIndOnMatterThread,
                                                 reinterpret_cast<intptr_t>(ctx));
        break;
    }

    case LOCK_MSG_DOOR_IND:
        LOG_INF("DOOR_IND state=0x%02x", payload1);
        /* TODO Phase 2: DoorLockServer::Instance().SetDoorState() */
        lock_fw_send_ack(LOCK_MSG_DOOR_ACK, LOCK_RESULT_OK, 0);
        break;

    case LOCK_MSG_FAULT_IND:
        LOG_WRN("FAULT_IND code=0x%02x", payload1);
        /* TODO Phase 2: DoorLockServer::Instance().SendLockAlarmEvent() */
        lock_fw_send_ack(LOCK_MSG_FAULT_ACK, LOCK_RESULT_OK, 0);
        break;

    case LOCK_MSG_LOCK_IND:
        LOG_INF("LOCK_IND — local lock triggered");
        lock_fw_send_ack(LOCK_MSG_LIND_ACK, LOCK_RESULT_OK, 0);
        DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) {
            DoorLockServer::Instance().SetLockState(
                s_endpoint, DlLockState::kLocked,
                OperationSourceEnum::kManual,
                NullNullable, NullNullable,
                NullNullable, NullNullable);
        }, 0);
        break;

    case LOCK_MSG_ENROLL_IND: {
        /* Format: [0x85][result:1B] */
        LOG_INF("ENROLL_IND result=%d", payload1);
        lock_fw_send_ack(LOCK_MSG_ENROLL_IND_ACK, LOCK_RESULT_OK, 0);
        /* Enrollment assumed success at this stage — no Matter state change needed
         * (credential slot already marked kOccupied at SetCredential time). */
        break;
    }

    default:
        LOG_WRN("Unknown IND type: 0x%02x", type);
        break;
    }
}

static void lock_ring_rx_callback(uint8_t ring_id, void *user_data)
{
    int len;

    do {
        len = ring_recv(ring_id, s_rx_buf, sizeof(s_rx_buf), K_NO_WAIT);
        if (len <= 0) break;

        uint8_t type   = s_rx_buf[0];
        uint8_t result = (len > 1) ? s_rx_buf[1] : LOCK_RESULT_OK;

        uint8_t dir     = type >> 7;
        uint8_t msgtype = (type >> 5) & 0x3;
        uint8_t subtype = type & 0x1F;

        if (dir != LOCK_DIR_HOST) {
            LOG_WRN("Unexpected FW→Host msg on RX: 0x%02x", type);
            continue;
        }
        switch (msgtype) {
        case LOCK_TYPE_IND:
            handle_host_ind(type, s_rx_buf, len);
            break;
        case LOCK_TYPE_ACK:
            handle_host_ack(type, result);
            break;
        case LOCK_TYPE_CTL:
            if (subtype == LOCK_SUBTYPE_CTL_SLEEP_OK) {
                LOG_INF("SLEEP_OK — releasing S2RAM lock");
                pm_policy_state_lock_put(PM_STATE_SUSPEND_TO_RAM, PM_ALL_SUBSTATES);
            } else {
                LOG_WRN("Unknown CTL subtype: 0x%02x (msg=0x%02x)", subtype, type);
            }
            break;
        default:
            LOG_WRN("Unknown type: 0x%02x (msg=0x%02x)", msgtype, type);
            break;
        }

    } while (len > 0);
}

/* =========================================================
 * PM notifier — S2RAM wakeup via external GPIO (Host→QCC730)
 * ========================================================= */

static void lock_pm_state_entry(enum pm_state state)
{
    ARG_UNUSED(state);
    if (spi_is_ext_wakeup()) {
        spi_clear_ext_wakeup_flag();
    }
}

/* k_work handler — runs in system work queue (non-ISR context) */
static void ext_wakeup_work_fn(struct k_work *work)
{
#if CHIP_CONFIG_ENABLE_ICD_SERVER
    chip::DeviceLayer::SystemLayer().ScheduleLambda([]() {
        chip::app::ICDNotifier::GetInstance()
            .NotifyNetworkActivityNotification();
        printk("[LOCK_FW] ICD network activity (ext GPIO wakeup)\n");
    });
#endif
}

static K_WORK_DEFINE(s_ext_wakeup_work, ext_wakeup_work_fn);

/**
 * Called from aon_ext_interrupt_wake_up() (ISR context) when
 * CONFIG_MATTER_LOCK_APP=y.  Runs for both S2RAM and already-awake cases.
 */
extern "C" void lock_fw_on_ext_wakeup(void)
{
    /* ISR-safe: spinlock + atomic */
    pm_policy_state_lock_get(PM_STATE_SUSPEND_TO_RAM, PM_ALL_SUBSTATES);
    /* Defer CHIP-stack work to system work queue */
    k_work_submit(&s_ext_wakeup_work);
}

static struct pm_notifier lock_pm_notifier = {
    .state_entry = lock_pm_state_entry,
    .state_exit  = nullptr,
};

/* =========================================================
 * Public API
 * ========================================================= */

CHIP_ERROR lock_fw_init(chip::EndpointId endpoint)
{
    gPendingOp.pending = false;

    /* Register PM notifier for S2RAM external-GPIO wakeup */
    pm_notifier_register(&lock_pm_notifier);

    int ret = ring_register_callback(lock_ring_rx_callback, nullptr);
    if (ret != 0) {
        ChipLogError(NotSpecified, "lock_fw: ring_register_callback failed: %d", ret);
        return CHIP_ERROR_INTERNAL;
    }

    /* Sync current LockState to Host.
     * Server::Init() already restored LockState from NVM before this call.
     * Send LOCK_CMD or UNLOCK_CMD so Host LED/hardware matches Matter state. */
    Nullable<DlLockState> lockState;
    if (chip::app::Clusters::DoorLock::Attributes::LockState::Get(endpoint, lockState)
            == chip::Protocols::InteractionModel::Status::Success
        && !lockState.IsNull())
    {
        ChipLogProgress(NotSpecified, "lock_fw: NVM LockState = %d (1=Locked 2=Unlocked 3=Unlatched)",
                        (int)lockState.Value());

        uint8_t syncCmd = (lockState.Value() == DlLockState::kLocked)
                          ? LOCK_MSG_LOCK_CMD
                          : LOCK_MSG_UNLOCK_CMD;

        ring_send(LOCK_FW_RING_ID, &syncCmd, 1, K_MSEC(200));
        ChipLogProgress(NotSpecified, "lock_fw: synced state to Host (cmd=0x%02x state=%d)",
                        syncCmd, (int)lockState.Value());
    }
    else
    {
        ChipLogProgress(NotSpecified, "lock_fw: LockState not available, skipping sync");
    }

    ChipLogProgress(NotSpecified, "lock_fw: initialized (host mode, ring=%d, endpoint=%d)",
                    LOCK_FW_RING_ID, endpoint);

    return CHIP_NO_ERROR;
}

int lock_fw_send_cmd(uint8_t cmd, const LockCmdCtx & cmdCtx)
{
    if (gPendingOp.pending) {
        ChipLogError(NotSpecified, "lock_fw: operation already pending (cmd=0x%02x)", gPendingOp.cmd);
        return -EBUSY;
    }

    /* Build wire message: bolt cmds are 1B, ENROLL_REQ carries credentialIndex (3B) */
    uint8_t msg[3];
    uint8_t msg_len;
    if (cmd == LOCK_MSG_ENROLL_REQ) {
        msg[0] = cmd;
        msg[1] = (uint8_t)(cmdCtx.enroll.credentialIndex >> 8);
        msg[2] = (uint8_t)(cmdCtx.enroll.credentialIndex & 0xFF);
        msg_len = 3;
    } else {
        msg[0]  = cmd;
        msg_len = 1;
    }

    int ret = ring_send(LOCK_FW_RING_ID, msg, msg_len, K_MSEC(100));
    if (ret != 0) {
        LOG_ERR("ring_send cmd=0x%02x failed: %d", cmd, ret);
        return ret;
    }

    gPendingOp.cmd     = cmd;
    gPendingOp.ctx     = cmdCtx;
    gPendingOp.pending = true;   /* must be last */

    chip::DeviceLayer::SystemLayer().StartTimer(
        System::Clock::Milliseconds32(LOCK_FW_RSP_TIMEOUT_MS),
        OnRspTimeout, nullptr);

    ChipLogProgress(NotSpecified, "lock_fw: cmd=0x%02x sent, waiting ACK (timeout=%dms)",
                    cmd, LOCK_FW_RSP_TIMEOUT_MS);
    return 0;
}
