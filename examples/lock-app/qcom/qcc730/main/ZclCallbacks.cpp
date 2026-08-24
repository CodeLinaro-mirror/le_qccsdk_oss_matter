/*
 * ZclCallbacks.cpp — QCC730 lock app ZCL callbacks
 *
 * All lock/unlock logic is delegated to QcomLockManager.
 * Credential/schedule management stays in lock-common LockManager.
 */

#include <app/clusters/door-lock-server/door-lock-server.h>
#include <app/data-model/Nullable.h>
#include <lib/core/DataModelTypes.h>
#include <lib/support/logging/CHIPLogging.h>

#include "LockManager.h"
#include "QcomLockManager.h"
#include "lock_fw.h"

using namespace chip;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::DoorLock;
using chip::app::DataModel::Nullable;

/* ---- Lock / Unlock / Unbolt ---- */

bool emberAfPluginDoorLockOnDoorLockCommand(chip::EndpointId endpointId,
                                            const Nullable<chip::FabricIndex> & fabricIdx,
                                            const Nullable<chip::NodeId> & nodeId,
                                            const Optional<ByteSpan> & pinCode,
                                            OperationErrorEnum & err)
{
    if (!QcomLockMgr().ValidatePIN(endpointId, pinCode, err))
        return false;
#if CONFIG_LOCK_HOST_MODE
    return QcomLockMgr().SendLockCmd(endpointId, OperationSourceEnum::kRemote,
                                      fabricIdx, nodeId);
#else
    ChipLogProgress(Zcl, "QcomLock: lock done");
    DoorLockServer::Instance().SetLockState(endpointId, DlLockState::kLocked,
                                            OperationSourceEnum::kRemote,
                                            NullNullable, NullNullable,
                                            fabricIdx, nodeId);
    return true;
#endif
}

bool emberAfPluginDoorLockOnDoorUnlockCommand(chip::EndpointId endpointId,
                                              const Nullable<chip::FabricIndex> & fabricIdx,
                                              const Nullable<chip::NodeId> & nodeId,
                                              const Optional<ByteSpan> & pinCode,
                                              OperationErrorEnum & err)
{
    if (!QcomLockMgr().ValidatePIN(endpointId, pinCode, err))
        return false;
#if CONFIG_LOCK_HOST_MODE
    return QcomLockMgr().SendUnlockCmd(endpointId, OperationSourceEnum::kRemote,
                                        fabricIdx, nodeId);
#else
    DlLockState targetState = DoorLockServer::Instance().SupportsUnbolt(endpointId)
                              ? DlLockState::kUnlatched : DlLockState::kUnlocked;
    ChipLogProgress(Zcl, "QcomLock: unlock done");
    DoorLockServer::Instance().SetLockState(endpointId, targetState,
                                            OperationSourceEnum::kRemote,
                                            NullNullable, NullNullable,
                                            fabricIdx, nodeId);
    return true;
#endif
}

bool emberAfPluginDoorLockOnDoorUnboltCommand(chip::EndpointId endpointId,
                                              const Nullable<chip::FabricIndex> & fabricIdx,
                                              const Nullable<chip::NodeId> & nodeId,
                                              const Optional<ByteSpan> & pinCode,
                                              OperationErrorEnum & err)
{
    if (!QcomLockMgr().ValidatePIN(endpointId, pinCode, err))
        return false;
#if CONFIG_LOCK_HOST_MODE
    return QcomLockMgr().SendUnboltCmd(endpointId, OperationSourceEnum::kRemote,
                                        fabricIdx, nodeId);
#else
    ChipLogProgress(Zcl, "QcomLock: unbolt done");
    DoorLockServer::Instance().SetLockState(endpointId, DlLockState::kUnlocked,
                                            OperationSourceEnum::kRemote,
                                            NullNullable, NullNullable,
                                            fabricIdx, nodeId);
    return true;
#endif
}

/* ---- User / Credential / Schedule (unchanged, stay in lock-common) ---- */

bool emberAfPluginDoorLockGetUser(chip::EndpointId endpointId, uint16_t userIndex,
                                  EmberAfPluginDoorLockUserInfo & user)
{
    return LockManager::Instance().GetUser(endpointId, userIndex, user);
}

bool emberAfPluginDoorLockSetUser(chip::EndpointId endpointId, uint16_t userIndex,
                                  chip::FabricIndex creator, chip::FabricIndex modifier,
                                  const chip::CharSpan & userName, uint32_t uniqueId,
                                  UserStatusEnum userStatus, UserTypeEnum usertype,
                                  CredentialRuleEnum credentialRule,
                                  const CredentialStruct * credentials, size_t totalCredentials)
{
    bool ok = LockManager::Instance().SetUser(endpointId, userIndex, creator, modifier,
                                              userName, uniqueId, userStatus, usertype,
                                              credentialRule, credentials, totalCredentials);
    if (ok) {
        if (userStatus == UserStatusEnum::kAvailable) {
            QcomLockMgr().UserPersistDelete(userIndex);
        } else {
            EmberAfPluginDoorLockUserInfo user;
            if (LockManager::Instance().GetUser(endpointId, userIndex, user))
                QcomLockMgr().UserPersistSave(endpointId, userIndex, user);
        }
    }
    return ok;
}

bool emberAfPluginDoorLockGetCredential(chip::EndpointId endpointId, uint16_t credentialIndex,
                                        CredentialTypeEnum credentialType,
                                        EmberAfPluginDoorLockCredentialInfo & credential)
{
    bool ok = LockManager::Instance().GetCredential(endpointId, credentialIndex,
                                                    credentialType, credential);
    /* Fingerprint credentialData is always empty (biometric template lives on Host).
     * The door-lock-server duplicate-data check would reject any second fingerprint
     * credential. Return credentialIndex as fake data so each slot looks unique. */
    if (ok && credentialType == CredentialTypeEnum::kFingerprint &&
        credential.status == DlCredentialStatus::kOccupied &&
        credential.credentialData.size() == 0)
    {
        static uint8_t s_fp_fake_data[2];
        s_fp_fake_data[0] = (uint8_t)(credentialIndex >> 8);
        s_fp_fake_data[1] = (uint8_t)(credentialIndex & 0xFF);
        credential.credentialData = chip::ByteSpan(s_fp_fake_data, sizeof(s_fp_fake_data));
    }
    return ok;
}

bool emberAfPluginDoorLockSetCredential(chip::EndpointId endpointId, uint16_t credentialIndex,
                                        chip::FabricIndex creator, chip::FabricIndex modifier,
                                        DlCredentialStatus credentialStatus,
                                        CredentialTypeEnum credentialType,
                                        const chip::ByteSpan & credentialData)
{
#if !CONFIG_LOCK_HOST_MODE
    if (credentialType == CredentialTypeEnum::kFingerprint) {
        ChipLogError(Zcl, "ZclCallbacks: fingerprint credential not supported in hostless mode");
        return false;
    }
#endif

    bool ok = LockManager::Instance().SetCredential(endpointId, credentialIndex, creator,
                                                    modifier, credentialStatus, credentialType,
                                                    credentialData);
    if (ok) {
        if (credentialStatus == DlCredentialStatus::kAvailable) {
            QcomLockMgr().CredPersistDelete(credentialIndex, credentialType);
        } else {
            QcomLockMgr().CredPersistSave(endpointId, credentialIndex,
                                           credentialStatus, credentialType, credentialData);
#if CONFIG_LOCK_HOST_MODE
            /* For Fingerprint: trigger physical enrollment on Host.
             * Credential is stored optimistically; Host will enroll the biometric
             * and reply with ENROLL_RSP. Identify will fail until enrollment succeeds. */
            if (credentialType == CredentialTypeEnum::kFingerprint) {
                LockCmdCtx ctx;
                ctx.endpoint               = endpointId;
                ctx.enroll.credentialIndex = credentialIndex;
                lock_fw_send_cmd(LOCK_MSG_ENROLL_REQ, ctx);
            }
#endif /* CONFIG_LOCK_HOST_MODE */
        }
    }
    return ok;
}

DlStatus emberAfPluginDoorLockGetSchedule(chip::EndpointId endpointId, uint8_t weekdayIndex,
                                          uint16_t userIndex,
                                          EmberAfPluginDoorLockWeekDaySchedule & schedule)
{
    return LockManager::Instance().GetSchedule(endpointId, weekdayIndex, userIndex, schedule);
}

DlStatus emberAfPluginDoorLockGetSchedule(chip::EndpointId endpointId, uint8_t holidayIndex,
                                          EmberAfPluginDoorLockHolidaySchedule & schedule)
{
    return LockManager::Instance().GetSchedule(endpointId, holidayIndex, schedule);
}

DlStatus emberAfPluginDoorLockSetSchedule(chip::EndpointId endpointId, uint8_t weekdayIndex,
                                          uint16_t userIndex, DlScheduleStatus status,
                                          DaysMaskMap daysMask, uint8_t startHour,
                                          uint8_t startMinute, uint8_t endHour, uint8_t endMinute)
{
    return LockManager::Instance().SetSchedule(endpointId, weekdayIndex, userIndex, status,
                                               daysMask, startHour, startMinute,
                                               endHour, endMinute);
}

DlStatus emberAfPluginDoorLockSetSchedule(chip::EndpointId endpointId, uint8_t yearDayIndex,
                                          uint16_t userIndex, DlScheduleStatus status,
                                          uint32_t localStartTime, uint32_t localEndTime)
{
    return LockManager::Instance().SetSchedule(endpointId, yearDayIndex, userIndex, status,
                                               localStartTime, localEndTime);
}

DlStatus emberAfPluginDoorLockGetSchedule(chip::EndpointId endpointId, uint8_t yearDayIndex,
                                          uint16_t userIndex,
                                          EmberAfPluginDoorLockYearDaySchedule & schedule)
{
    return LockManager::Instance().GetSchedule(endpointId, yearDayIndex, userIndex, schedule);
}

DlStatus emberAfPluginDoorLockSetSchedule(chip::EndpointId endpointId, uint8_t holidayIndex,
                                          DlScheduleStatus status, uint32_t localStartTime,
                                          uint32_t localEndTime, OperatingModeEnum operatingMode)
{
    return LockManager::Instance().SetSchedule(endpointId, holidayIndex, status,
                                               localStartTime, localEndTime, operatingMode);
}

void emberAfDoorLockClusterInitCallback(EndpointId endpoint)
{
    DoorLockServer::Instance().InitServer(endpoint);
    LockManager::Instance().InitEndpoint(endpoint);
    /* Restore credentials and users from NVM (must be after InitEndpoint). */
    QcomLockMgr().UserPersistLoad(endpoint);
    QcomLockMgr().CredPersistLoad(endpoint);
}

/**
 * @brief AutoRelock callback — called by DoorLockServer after it has already
 *        set LockState=kLocked internally. We just notify the Host to drive
 *        the motor. Fire-and-forget: if Host fails, Matter state stays Locked.
 */
void emberAfPluginDoorLockOnAutoRelock(chip::EndpointId endpointId)
{
#if CONFIG_LOCK_HOST_MODE
    ChipLogProgress(Zcl, "AutoRelock: notifying Host to lock");
    LockCmdCtx ctx;
    ctx.endpoint         = endpointId;
    ctx.bolt.targetState = DlLockState::kLocked;
    ctx.bolt.opSource    = OperationSourceEnum::kAuto;
    lock_fw_send_cmd(LOCK_MSG_LOCK_CMD, ctx);
#else
    ChipLogProgress(Zcl, "AutoRelock: locking");
    DoorLockServer::Instance().SetLockState(endpointId, DlLockState::kLocked,
                                            OperationSourceEnum::kAuto,
                                            NullNullable, NullNullable,
                                            NullNullable, NullNullable);
#endif
}

/* =========================================================
 * Biometric credential count — required to enable Fingerprint
 * credential support (without this, index range check fails).
 * ========================================================= */
bool emberAfPluginDoorLockGetNumberOfFingerprintCredentialsSupported(
    chip::EndpointId endpointId, uint16_t & maxNumberOfCredentials)
{
    maxNumberOfCredentials = 10;
    return true;
}

bool emberAfPluginDoorLockGetFingerprintCredentialLengthConstraints(
    chip::EndpointId endpointId, uint8_t & minLen, uint8_t & maxLen)
{
    /* AS608 fingerprint: credential data is empty (biometric validated by Host).
     * Allow any length 0..0 so empty credential data passes validation. */
    minLen = 0;
    maxLen = 0;
    return true;
}

void MatterPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & path,
                                        uint8_t type, uint16_t size, uint8_t * value)
{
    if (path.mClusterId != chip::app::Clusters::DoorLock::Id)
        return;
    if (path.mAttributeId != chip::app::Clusters::DoorLock::Attributes::LockState::Id)
        return;
    if (size < 1)
        return;

    uint8_t state = *value;
    const char * stateStr = (state == 1) ? "Locked" :
                            (state == 2) ? "Unlocked" :
                            (state == 3) ? "Unlatched" : "Unknown";
    ChipLogProgress(Zcl, "LockState → %s (%u)", stateStr, state);
}
