/*
 * lock_fw.h — QCC730 FW side ring service interface for lock app
 *
 * Message encoding (bit-field layout):
 *
 *   FW → Host (Bit7=0):
 *     Bits[6:5]=00  CMD  (0x01–0x1F)  — requests to Host
 *     Bits[6:5]=01  ACK  (0x21–0x3F)  — FW's ACK for a Host IND
 *
 *   Host → FW (Bit7=1):
 *     Bits[6:5]=00  IND  (0x81–0x9F)  — event notifications from Host
 *     Bits[6:5]=01  ACK  (0xA1–0xBF)  — Host's ACK for a FW CMD
 *     Bits[6:5]=10  CTL  (0xC0–0xDF)  — control (SLEEP_OK)
 *
 * CMD → ACK correlation:
 *   LOCK_CMD   (0x01) → LOCK_ACK   (0xA1)
 *   UNLOCK_CMD (0x02) → UNLOCK_ACK (0xA2)
 *   UNBOLT_CMD (0x03) → UNLOCK_ACK (0xA2)
 *   ENROLL_REQ (0x04) → ENROLL_ACK (0xA3)  (immediate, before enrollment starts)
 *
 * IND → ACK correlation:
 *   CRED_IND   (0x81) → CRED_ACK   (0x21)  payload: [result][opSource]
 *   DOOR_IND   (0x82) → DOOR_ACK   (0x22)  payload: [result]
 *   FAULT_IND  (0x83) → FAULT_ACK  (0x23)  payload: [result]
 *   LOCK_IND   (0x84) → LIND_ACK   (0x24)  payload: [result]
 *   ENROLL_IND (0x85) → ENROLL_IND_ACK (0x25)  payload: [result]
 *
 * CRED_IND / CRED_ACK flow (GPIO wakeup session):
 *   1. Host GPIO-wakes FW, sets s_gpio_wakeup_active=true.
 *   2. Host sends CRED_IND.
 *   3. FW validates credential, sends CRED_ACK[result][opSource].
 *      (FW calls spi_clear_ext_wakeup_flag() inside lock_fw_send_ack.)
 *   4. Host receives CRED_ACK → sends SLEEP_OK (always).
 *   5. FW receives SLEEP_OK → pm_policy_state_lock_put.
 *   6. If CRED_ACK result=OK: FW sends UNLOCK_CMD → Host replies UNLOCK_ACK.
 *
 * ENROLL_IND flow (GPIO wakeup session, after AS608 enrollment completes):
 *   1. Host GPIO-wakes FW.
 *   2. Host sends ENROLL_IND[result].
 *   3. FW updates Matter credential state, sends ENROLL_IND_ACK.
 *   4. Host sends SLEEP_OK → FW releases S2RAM lock.
 *
 * GPIO wakeup / PM lifecycle:
 *   Host pulls GPIO5 (PB8) LOW→HIGH to wake QCC730 from S2RAM.
 *   FW: ISR calls pm_policy_state_lock_get to hold S2RAM lock.
 *   FW: sends CRED_ACK → Host sends SLEEP_OK → FW releases pm lock.
 *   For Remote Matter commands (no GPIO pull), Host never sends SLEEP_OK.
 *   S2RAM min_residency (~100ms) provides safety window for UNLOCK_CMD
 *   round-trip after SLEEP_OK.
 */

#pragma once

#include <lib/core/DataModelTypes.h>
#include <lib/core/CHIPError.h>
#include <app/clusters/door-lock-server/door-lock-server.h>
#include <app/data-model/Nullable.h>

/* Ring ID used for lock app messages */
#define LOCK_FW_RING_ID  0

/* Direction (bit7) */
#define LOCK_DIR_FW    0   /* FW → Host */
#define LOCK_DIR_HOST  1   /* Host → FW */

/* Type (bits[6:5]) */
#define LOCK_TYPE_CMD  0   /* FW → Host: command */
#define LOCK_TYPE_ACK  1   /* Both:      acknowledgement */
#define LOCK_TYPE_IND  0   /* Host → FW: indication (same encoding as CMD, different direction) */
#define LOCK_TYPE_CTL  2   /* Host → FW: control */

/* Subtype (bits[4:0]) — CMD */
#define LOCK_SUBTYPE_CMD_LOCK    1
#define LOCK_SUBTYPE_CMD_UNLOCK  2
#define LOCK_SUBTYPE_CMD_UNBOLT  3
#define LOCK_SUBTYPE_CMD_ENROLL  4

/* Subtype (bits[4:0]) — IND */
#define LOCK_SUBTYPE_IND_CRED    1
#define LOCK_SUBTYPE_IND_DOOR    2
#define LOCK_SUBTYPE_IND_FAULT   3
#define LOCK_SUBTYPE_IND_LOCK    4
#define LOCK_SUBTYPE_IND_ENROLL  5

/* Subtype (bits[4:0]) — ACK (FW→Host: response to IND) */
#define LOCK_SUBTYPE_ACK_CRED       1
#define LOCK_SUBTYPE_ACK_DOOR       2
#define LOCK_SUBTYPE_ACK_FAULT      3
#define LOCK_SUBTYPE_ACK_LIND       4
#define LOCK_SUBTYPE_ACK_ENROLL_IND 5
/* Subtype (bits[4:0]) — ACK (Host→FW: response to CMD) */
#define LOCK_SUBTYPE_ACK_LOCK       1
#define LOCK_SUBTYPE_ACK_UNLOCK     2
#define LOCK_SUBTYPE_ACK_ENROLL     3

/* Subtype (bits[4:0]) — CTL */
#define LOCK_SUBTYPE_CTL_SLEEP_OK   0

/* Message byte constructor */
#define LOCK_MSG(dir, type, subtype) \
    (uint8_t)(((dir) << 7) | ((type) << 5) | (subtype))

/* ---- FW → Host CMD ---- */
#define LOCK_MSG_LOCK_CMD    LOCK_MSG(LOCK_DIR_FW,   LOCK_TYPE_CMD, LOCK_SUBTYPE_CMD_LOCK)
#define LOCK_MSG_UNLOCK_CMD  LOCK_MSG(LOCK_DIR_FW,   LOCK_TYPE_CMD, LOCK_SUBTYPE_CMD_UNLOCK)
#define LOCK_MSG_UNBOLT_CMD  LOCK_MSG(LOCK_DIR_FW,   LOCK_TYPE_CMD, LOCK_SUBTYPE_CMD_UNBOLT)
#define LOCK_MSG_ENROLL_REQ  LOCK_MSG(LOCK_DIR_FW,   LOCK_TYPE_CMD, LOCK_SUBTYPE_CMD_ENROLL)

/* ---- FW → Host ACK (response to Host IND) ---- */
#define LOCK_MSG_CRED_ACK        LOCK_MSG(LOCK_DIR_FW,   LOCK_TYPE_ACK, LOCK_SUBTYPE_ACK_CRED)
#define LOCK_MSG_DOOR_ACK        LOCK_MSG(LOCK_DIR_FW,   LOCK_TYPE_ACK, LOCK_SUBTYPE_ACK_DOOR)
#define LOCK_MSG_FAULT_ACK       LOCK_MSG(LOCK_DIR_FW,   LOCK_TYPE_ACK, LOCK_SUBTYPE_ACK_FAULT)
#define LOCK_MSG_LIND_ACK        LOCK_MSG(LOCK_DIR_FW,   LOCK_TYPE_ACK, LOCK_SUBTYPE_ACK_LIND)
#define LOCK_MSG_ENROLL_IND_ACK  LOCK_MSG(LOCK_DIR_FW,   LOCK_TYPE_ACK, LOCK_SUBTYPE_ACK_ENROLL_IND)

/* ---- Host → FW IND ---- */
#define LOCK_MSG_CRED_IND    LOCK_MSG(LOCK_DIR_HOST, LOCK_TYPE_IND, LOCK_SUBTYPE_IND_CRED)
#define LOCK_MSG_DOOR_IND    LOCK_MSG(LOCK_DIR_HOST, LOCK_TYPE_IND, LOCK_SUBTYPE_IND_DOOR)
#define LOCK_MSG_FAULT_IND   LOCK_MSG(LOCK_DIR_HOST, LOCK_TYPE_IND, LOCK_SUBTYPE_IND_FAULT)
#define LOCK_MSG_LOCK_IND    LOCK_MSG(LOCK_DIR_HOST, LOCK_TYPE_IND, LOCK_SUBTYPE_IND_LOCK)
#define LOCK_MSG_ENROLL_IND  LOCK_MSG(LOCK_DIR_HOST, LOCK_TYPE_IND, LOCK_SUBTYPE_IND_ENROLL)

/* ---- Host → FW ACK (response to FW CMD) ---- */
#define LOCK_MSG_LOCK_ACK    LOCK_MSG(LOCK_DIR_HOST, LOCK_TYPE_ACK, LOCK_SUBTYPE_ACK_LOCK)
#define LOCK_MSG_UNLOCK_ACK  LOCK_MSG(LOCK_DIR_HOST, LOCK_TYPE_ACK, LOCK_SUBTYPE_ACK_UNLOCK)
#define LOCK_MSG_ENROLL_ACK  LOCK_MSG(LOCK_DIR_HOST, LOCK_TYPE_ACK, LOCK_SUBTYPE_ACK_ENROLL)

/* ---- Host → FW CTL ---- */
#define LOCK_MSG_SLEEP_OK    LOCK_MSG(LOCK_DIR_HOST, LOCK_TYPE_CTL, LOCK_SUBTYPE_CTL_SLEEP_OK)

/* Result codes for RSP messages */
#define LOCK_RESULT_OK   0x00
#define LOCK_RESULT_ERR  0x01

/* ACK timeout for all commands (LOCK/UNLOCK/UNBOLT/ENROLL_REQ) */
#define LOCK_FW_RSP_TIMEOUT_MS     3000

/**
 * Command context passed to lock_fw_send_cmd().
 * Union selects between bolt (LOCK/UNLOCK/UNBOLT) and enroll (ENROLL_REQ) payload.
 * fabricIdx/nodeId sit outside the union (non-trivial constructors).
 */
struct LockCmdCtx {
    chip::EndpointId endpoint = 0;
    union {
        struct {   /* LOCK_CMD / UNLOCK_CMD / UNBOLT_CMD */
            chip::app::Clusters::DoorLock::DlLockState         targetState;
            chip::app::Clusters::DoorLock::OperationSourceEnum opSource;
        } bolt;
        struct {   /* ENROLL_REQ */
            uint16_t credentialIndex;
        } enroll;
    };
    chip::app::DataModel::Nullable<chip::FabricIndex> fabricIdx;
    chip::app::DataModel::Nullable<chip::NodeId>      nodeId;
};

/**
 * @brief Send a command to Host and track the pending operation.
 *
 *   Bolt cmds (LOCK/UNLOCK/UNBOLT): ctx.bolt.targetState / opSource / fabricIdx / nodeId
 *   Enroll cmd (ENROLL_REQ):        ctx.enroll.credentialIndex
 *
 * Wire format:
 *   Bolt  : [cmd:1B]
 *   Enroll: [cmd:1B][credIdx_hi:1B][credIdx_lo:1B]
 *
 * Timeout: LOCK_FW_RSP_TIMEOUT_MS for bolt, LOCK_FW_ENROLL_TIMEOUT_MS for enroll.
 *
 * @return 0       command sent, waiting for ACK
 *         -EBUSY  another operation is already pending
 *         <0      ring_send failed
 */
#if CONFIG_LOCK_HOST_MODE
int lock_fw_send_cmd(uint8_t cmd, const LockCmdCtx & ctx);

/**
 * @brief Send ACK for a Host IND message (host mode only).
 *        Calls spi_clear_ext_wakeup_flag() before ring_send.
 */
int lock_fw_send_ack(uint8_t type, uint8_t result, uint8_t opSource);

/**
 * @brief Initialize lock FW ring service (host mode only).
 */
CHIP_ERROR lock_fw_init(chip::EndpointId endpoint);
#endif /* CONFIG_LOCK_HOST_MODE */
