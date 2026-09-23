/*
 * QcomLockManager.cpp — QCC730 platform lock manager implementation
 */

#include "QcomLockManager.h"
#include "lock_fw.h"
#include "LockManager.h"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/KeyValueStoreManager.h>

#include <stdio.h>
#include <string.h>

using namespace chip::DeviceLayer::PersistedStorage;

using namespace chip;
using namespace chip::app::Clusters::DoorLock;
using chip::app::DataModel::Nullable;

QcomLockManager QcomLockManager::sInstance;

QcomLockManager & QcomLockManager::Instance()
{
    return sInstance;
}

void QcomLockManager::Init()
{
}

/* =========================================================
 * PIN Validation
 *
 * Mirrors nrfconnect's ValidatePIN pattern, but uses lock-common's
 * LockManager::GetCredential() to iterate the credential store
 * instead of a local copy.
 * ========================================================= */

bool QcomLockManager::ValidatePIN(EndpointId endpoint,
                                   const Optional<ByteSpan> & pin,
                                   OperationErrorEnum & err) const
{
    // If no PIN provided, check whether it is required
    if (!pin.HasValue())
    {
        bool requirePin = false;
        Attributes::RequirePINforRemoteOperation::Get(endpoint, &requirePin);
        if (requirePin)
        {
            ChipLogError(Zcl, "QcomLock: PIN required but not provided");
            err = OperationErrorEnum::kInvalidCredential;
            return false;
        }
        return true;
    }

    // PIN provided — search the credential store
    uint16_t maxPINs = 10;
    Attributes::NumberOfPINUsersSupported::Get(endpoint, &maxPINs);

    ChipLogDetail(Zcl, "QcomLock: ValidatePIN searching %d slots for pin len=%u",
                  maxPINs, (unsigned)pin.Value().size());

    for (uint16_t i = 1; i <= maxPINs; i++)
    {
        EmberAfPluginDoorLockCredentialInfo cred;
        if (!LockManager::Instance().GetCredential(endpoint, i, CredentialTypeEnum::kPin, cred))
        {
            continue;
        }

        ChipLogDetail(Zcl, "QcomLock: slot[%d] status=%d dataLen=%u",
                      i, (int)cred.status, (unsigned)cred.credentialData.size());

        if (cred.status == DlCredentialStatus::kAvailable)
        {
            continue;
        }
        if (cred.credentialData.data_equal(pin.Value()))
        {
            ChipLogDetail(Zcl, "QcomLock: valid PIN provided");
            return true;
        }
    }

    ChipLogDetail(Zcl, "QcomLock: invalid PIN provided");
    err = OperationErrorEnum::kInvalidCredential;
    return false;
}

bool QcomLockManager::ValidateBiometric(EndpointId endpoint,
                                         uint16_t credentialIndex,
                                         OperationErrorEnum & err) const
{
    EmberAfPluginDoorLockCredentialInfo cred;
    if (!LockManager::Instance().GetCredential(endpoint, credentialIndex,
                                                CredentialTypeEnum::kFingerprint, cred))
    {
        ChipLogDetail(Zcl, "QcomLock: biometric cred[%u] not found", credentialIndex);
        err = OperationErrorEnum::kInvalidCredential;
        return false;
    }
    if (cred.status == DlCredentialStatus::kAvailable)
    {
        ChipLogDetail(Zcl, "QcomLock: biometric cred[%u] available (not enrolled)", credentialIndex);
        err = OperationErrorEnum::kInvalidCredential;
        return false;
    }
    return true;
}

/* =========================================================
 * Send bolt commands (no validation — caller must validate PIN first)
 * Only used in Host mode; hostless handles lock/unlock directly in ZclCallbacks.
 * ========================================================= */
#if CONFIG_LOCK_HOST_MODE
bool QcomLockManager::SendLockCmd(EndpointId endpoint,
                                   OperationSourceEnum opSource,
                                   const Nullable<FabricIndex> & fabricIdx,
                                   const Nullable<NodeId> & nodeId)
{
    OperationErrorEnum err;
    return SendRequest(LOCK_MSG_LOCK_CMD, endpoint, DlLockState::kLocked,
                       opSource, fabricIdx, nodeId, err);
}

bool QcomLockManager::SendUnlockCmd(EndpointId endpoint,
                                     OperationSourceEnum opSource,
                                     const Nullable<FabricIndex> & fabricIdx,
                                     const Nullable<NodeId> & nodeId)
{
    DlLockState targetState = DoorLockServer::Instance().SupportsUnbolt(endpoint)
                              ? DlLockState::kUnlatched
                              : DlLockState::kUnlocked;
    OperationErrorEnum err;
    return SendRequest(LOCK_MSG_UNLOCK_CMD, endpoint, targetState,
                       opSource, fabricIdx, nodeId, err);
}

bool QcomLockManager::SendUnboltCmd(EndpointId endpoint,
                                     OperationSourceEnum opSource,
                                     const Nullable<FabricIndex> & fabricIdx,
                                     const Nullable<NodeId> & nodeId)
{
    OperationErrorEnum err;
    return SendRequest(LOCK_MSG_UNBOLT_CMD, endpoint, DlLockState::kUnlocked,
                       opSource, fabricIdx, nodeId, err);
}

/* =========================================================
 * Internal: send ring request
 * ========================================================= */

bool QcomLockManager::SendRequest(uint8_t ringCmd,
                                   EndpointId endpoint,
                                   DlLockState targetState,
                                   OperationSourceEnum opSource,
                                   const Nullable<FabricIndex> & fabricIdx,
                                   const Nullable<NodeId>      & nodeId,
                                   OperationErrorEnum & err)
{
    LockCmdCtx ctx;
    ctx.endpoint           = endpoint;
    ctx.bolt.targetState   = targetState;
    ctx.bolt.opSource      = opSource;
    ctx.fabricIdx          = fabricIdx;
    ctx.nodeId             = nodeId;

    int ret = lock_fw_send_cmd(ringCmd, ctx);
    if (ret != 0)
    {
        ChipLogError(Zcl, "QcomLock: ring send failed (cmd=0x%02x ret=%d)", ringCmd, ret);
        err = OperationErrorEnum::kUnspecified;
        return false;
    }

    return true;
}

/* =========================================================
 * RSP completion callback — called from lock_fw.cpp on CHIP thread
 * ========================================================= */
void QcomLockManager::OnOperationCompleted(bool success)
{
    if (!success)
    {
        ChipLogError(Zcl, "QcomLock: operation failed");
    }
}
#endif /* CONFIG_LOCK_HOST_MODE */

/* =========================================================
 * Credential / User NVM persistence
 *
 * Key format:
 *   "lk/c/<type>_<idx>"  → credential of given type at index idx
 *   "lk/u/<idx>"         → user at index idx
 *   "lk/cm"              → credential manifest (list of saved type+index pairs)
 *   "lk/um"              → user manifest (list of saved user indices)
 *
 * Manifests avoid O(n) NVS scans for non-existent keys:
 * CredPersistLoad reads the manifest first, then only fetches
 * the specific credentials/users that are known to exist.
 * ========================================================= */

/* Max credentials per user from ZAP (NumberOfCredentialsSupportedPerUser = 5) */
#define QCOM_LOCK_MAX_CREDS_PER_USER  5
#define QCOM_LOCK_MAX_CRED_MANIFEST   20  /* total across all types */
#define QCOM_LOCK_MAX_USER_MANIFEST   10

/* Persisted credential: only fields needed for access control */
struct PersistedCredential {
    uint8_t  status;
    uint8_t  type;
    uint8_t  dataLen;
    uint8_t  data[128];
};

/* Manifest of saved credentials */
struct CredManifest {
    uint8_t count;
    struct { uint8_t type; uint16_t idx; } entries[QCOM_LOCK_MAX_CRED_MANIFEST];
};

/* Manifest of saved users */
struct UserManifest {
    uint8_t  count;
    uint16_t indices[QCOM_LOCK_MAX_USER_MANIFEST];
};

/* Persisted user: minimal fields for credential→user mapping */
struct PersistedUser {
    uint8_t  status;
    uint8_t  userType;
    uint8_t  credentialRule;
    uint32_t uniqueId;
    uint8_t  nameLen;
    char     name[DOOR_LOCK_USER_NAME_BUFFER_SIZE];
    uint8_t  credCount;
    struct { uint8_t type; uint16_t index; } credList[QCOM_LOCK_MAX_CREDS_PER_USER];
};

static inline void cred_key(char * buf, uint8_t type, uint16_t idx)
{
    snprintf(buf, 16, "lk/c/%u_%u", type, idx);
}
static inline void user_key(char * buf, uint16_t idx) { snprintf(buf, 16, "lk/u/%u", idx); }

static CredManifest cred_manifest_read(void)
{
    CredManifest m{};
    size_t readLen;
    KeyValueStoreMgr().Get("lk/cm", &m, sizeof(m), &readLen);
    return m;
}

static void cred_manifest_add(uint8_t type, uint16_t idx)
{
    CredManifest m = cred_manifest_read();
    for (uint8_t i = 0; i < m.count; i++) {
        if (m.entries[i].type == type && m.entries[i].idx == idx) return; /* already present */
    }
    if (m.count < QCOM_LOCK_MAX_CRED_MANIFEST) {
        m.entries[m.count].type = type;
        m.entries[m.count].idx  = idx;
        m.count++;
        KeyValueStoreMgr().Put("lk/cm", &m, sizeof(m));
    }
}

static void cred_manifest_remove(uint8_t type, uint16_t idx)
{
    CredManifest m = cred_manifest_read();
    for (uint8_t i = 0; i < m.count; i++) {
        if (m.entries[i].type == type && m.entries[i].idx == idx) {
            m.entries[i] = m.entries[--m.count]; /* replace with last */
            KeyValueStoreMgr().Put("lk/cm", &m, sizeof(m));
            return;
        }
    }
}

static UserManifest user_manifest_read(void)
{
    UserManifest m{};
    size_t readLen;
    KeyValueStoreMgr().Get("lk/um", &m, sizeof(m), &readLen);
    return m;
}

static void user_manifest_add(uint16_t idx)
{
    UserManifest m = user_manifest_read();
    for (uint8_t i = 0; i < m.count; i++) {
        if (m.indices[i] == idx) return;
    }
    if (m.count < QCOM_LOCK_MAX_USER_MANIFEST) {
        m.indices[m.count++] = idx;
        KeyValueStoreMgr().Put("lk/um", &m, sizeof(m));
    }
}

static void user_manifest_remove(uint16_t idx)
{
    UserManifest m = user_manifest_read();
    for (uint8_t i = 0; i < m.count; i++) {
        if (m.indices[i] == idx) {
            m.indices[i] = m.indices[--m.count];
            KeyValueStoreMgr().Put("lk/um", &m, sizeof(m));
            return;
        }
    }
}

void QcomLockManager::CredPersistSave(chip::EndpointId endpoint, uint16_t credIdx,
                                       DlCredentialStatus status, CredentialTypeEnum type,
                                       const chip::ByteSpan & data)
{
    /* Fingerprint: biometric template lives on Host (AS608), not here.
     * data is always empty, so no NVS key-value needed — manifest entry alone
     * is sufficient to restore the occupied slot on boot. */
    if (type == CredentialTypeEnum::kFingerprint) {
        cred_manifest_add(chip::to_underlying(type), credIdx);
        ChipLogDetail(Zcl, "CredPersistSave[fp idx=%u] manifest only", credIdx);
        return;
    }

    PersistedCredential pc;
    pc.status  = chip::to_underlying(status);
    pc.type    = chip::to_underlying(type);
    pc.dataLen = static_cast<uint8_t>(data.size() < sizeof(pc.data) ? data.size() : sizeof(pc.data));
    memcpy(pc.data, data.data(), pc.dataLen);

    char key[16]; cred_key(key, chip::to_underlying(type), credIdx);
    CHIP_ERROR err = KeyValueStoreMgr().Put(key, &pc, sizeof(pc));
    if (err != CHIP_NO_ERROR) {
        ChipLogError(Zcl, "CredPersistSave[%u] failed: %" CHIP_ERROR_FORMAT, credIdx, err.Format());
    } else {
        cred_manifest_add(chip::to_underlying(type), credIdx);
        ChipLogDetail(Zcl, "CredPersistSave[%u] OK (type=%u len=%u)", credIdx, pc.type, pc.dataLen);
    }
}

void QcomLockManager::CredPersistDelete(uint16_t credIdx, CredentialTypeEnum type)
{
    if (type != CredentialTypeEnum::kFingerprint) {
        char key[16]; cred_key(key, chip::to_underlying(type), credIdx);
        KeyValueStoreMgr().Delete(key);
    }
    cred_manifest_remove(chip::to_underlying(type), credIdx);
    ChipLogDetail(Zcl, "CredPersistDelete[type=%u idx=%u]", chip::to_underlying(type), credIdx);
}

void QcomLockManager::UserPersistSave(chip::EndpointId endpoint, uint16_t userIdx,
                                       const EmberAfPluginDoorLockUserInfo & user)
{
    PersistedUser pu;
    pu.status         = chip::to_underlying(user.userStatus);
    pu.userType       = chip::to_underlying(user.userType);
    pu.credentialRule = chip::to_underlying(user.credentialRule);
    pu.uniqueId       = user.userUniqueId;
    pu.nameLen        = static_cast<uint8_t>(
        user.userName.size() < sizeof(pu.name) ? user.userName.size() : sizeof(pu.name) - 1);
    memcpy(pu.name, user.userName.data(), pu.nameLen);
    pu.name[pu.nameLen] = '\0';
    pu.credCount      = static_cast<uint8_t>(
        user.credentials.size() < QCOM_LOCK_MAX_CREDS_PER_USER
            ? user.credentials.size() : QCOM_LOCK_MAX_CREDS_PER_USER);
    for (uint8_t i = 0; i < pu.credCount; i++) {
        pu.credList[i].type  = chip::to_underlying(user.credentials.data()[i].credentialType);
        pu.credList[i].index = user.credentials.data()[i].credentialIndex;
    }

    char key[16]; user_key(key, userIdx);
    CHIP_ERROR err = KeyValueStoreMgr().Put(key, &pu, sizeof(pu));
    if (err != CHIP_NO_ERROR) {
        ChipLogError(Zcl, "UserPersistSave[%u] failed: %" CHIP_ERROR_FORMAT, userIdx, err.Format());
    } else {
        user_manifest_add(userIdx);
        ChipLogDetail(Zcl, "UserPersistSave[%u] OK", userIdx);
    }
}

void QcomLockManager::UserPersistDelete(uint16_t userIdx)
{
    char key[16]; user_key(key, userIdx);
    KeyValueStoreMgr().Delete(key);
    user_manifest_remove(userIdx);
    ChipLogDetail(Zcl, "UserPersistDelete[%u]", userIdx);
}

void QcomLockManager::CredPersistLoad(chip::EndpointId endpoint)
{
    CredManifest m = cred_manifest_read();
    uint16_t restored = 0;
    for (uint8_t i = 0; i < m.count; i++) {
        auto credType = static_cast<CredentialTypeEnum>(m.entries[i].type);

        if (credType == CredentialTypeEnum::kFingerprint) {
            /* Fingerprint: no NVS key-value — synthesize directly from manifest */
            LockManager::Instance().SetCredential(endpoint, m.entries[i].idx,
                0, 0,
                DlCredentialStatus::kOccupied,
                CredentialTypeEnum::kFingerprint,
                chip::ByteSpan());
            restored++;
            continue;
        }

        char key[16]; cred_key(key, m.entries[i].type, m.entries[i].idx);
        PersistedCredential pc;
        size_t readLen;
        if (KeyValueStoreMgr().Get(key, &pc, sizeof(pc), &readLen) == CHIP_NO_ERROR) {
            LockManager::Instance().SetCredential(endpoint, m.entries[i].idx,
                0, 0,
                static_cast<DlCredentialStatus>(pc.status),
                credType,
                chip::ByteSpan(pc.data, pc.dataLen));
            restored++;
        }
    }
    ChipLogProgress(Zcl, "CredPersistLoad: restored %u credentials", restored);
}

void QcomLockManager::UserPersistLoad(chip::EndpointId endpoint)
{
    UserManifest m = user_manifest_read();
    uint16_t restored = 0;
    for (uint8_t mi = 0; mi < m.count; mi++) {
        uint16_t i = m.indices[mi];
        char key[16]; user_key(key, i);
        PersistedUser pu;
        size_t readLen;
        if (KeyValueStoreMgr().Get(key, &pu, sizeof(pu), &readLen) == CHIP_NO_ERROR) {
            CredentialStruct creds[QCOM_LOCK_MAX_CREDS_PER_USER];
            for (uint8_t j = 0; j < pu.credCount; j++) {
                creds[j].credentialType  = static_cast<CredentialTypeEnum>(pu.credList[j].type);
                creds[j].credentialIndex = pu.credList[j].index;
            }
            LockManager::Instance().SetUser(endpoint, i,
                0, 0,
                chip::CharSpan(pu.name, pu.nameLen),
                pu.uniqueId,
                static_cast<UserStatusEnum>(pu.status),
                static_cast<UserTypeEnum>(pu.userType),
                static_cast<CredentialRuleEnum>(pu.credentialRule),
                creds, pu.credCount);
            restored++;
        }
    }
    ChipLogProgress(Zcl, "UserPersistLoad: restored %u users", restored);
}

