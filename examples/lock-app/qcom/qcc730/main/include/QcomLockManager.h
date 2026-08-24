/*
 * QcomLockManager.h — QCC730 platform lock manager
 *
 * Lock/Unlock/Unbolt operations are delegated to the Host via the ring
 * service; state is updated only after the Host ACK is received.
 */

#pragma once

#include <app/clusters/door-lock-server/door-lock-server.h>
#include <app/data-model/Nullable.h>
#include <lib/core/DataModelTypes.h>

using namespace chip::app::Clusters::DoorLock;
using chip::app::DataModel::Nullable;

class QcomLockManager
{
public:
    static QcomLockManager & Instance();

    void Init();

    /**
     * Validate PIN code against the credential store.
     * - If pin not provided and not required → true
     * - If pin provided → iterate lock-common credential store and match
     */
    bool ValidatePIN(chip::EndpointId endpoint,
                     const chip::Optional<chip::ByteSpan> & pin,
                     OperationErrorEnum & err) const;

    /**
     * Validate biometric credential (Fingerprint) by index.
     * Does NOT send any ring command; only checks the credential table.
     */
    bool ValidateBiometric(chip::EndpointId endpoint,
                            uint16_t credentialIndex,
                            OperationErrorEnum & err) const;

#if CONFIG_LOCK_HOST_MODE
    /**
     * Send bolt commands to Host (no PIN validation — caller must validate first).
     * fabricIdx/nodeId are NullNullable for local operations.
     */
    bool SendLockCmd(chip::EndpointId endpoint,
                     OperationSourceEnum opSource,
                     const Nullable<chip::FabricIndex> & fabricIdx = NullNullable,
                     const Nullable<chip::NodeId> & nodeId = NullNullable);

    bool SendUnlockCmd(chip::EndpointId endpoint,
                       OperationSourceEnum opSource,
                       const Nullable<chip::FabricIndex> & fabricIdx = NullNullable,
                       const Nullable<chip::NodeId> & nodeId = NullNullable);

    bool SendUnboltCmd(chip::EndpointId endpoint,
                       OperationSourceEnum opSource,
                       const Nullable<chip::FabricIndex> & fabricIdx = NullNullable,
                       const Nullable<chip::NodeId> & nodeId = NullNullable);
#endif /* CONFIG_LOCK_HOST_MODE */

#if CONFIG_LOCK_HOST_MODE
    /**
     * Called by lock_fw.cpp ACK handler (on CHIP thread) when Host operation completes.
     */
    void OnOperationCompleted(bool success);
#endif /* CONFIG_LOCK_HOST_MODE */

    /* ---- Credential / User NVM persistence ---- */

    void CredPersistSave(chip::EndpointId endpoint, uint16_t credIdx,
                         DlCredentialStatus status, CredentialTypeEnum type,
                         const chip::ByteSpan & data);
    void CredPersistDelete(uint16_t credIdx, chip::app::Clusters::DoorLock::CredentialTypeEnum type);
    void UserPersistSave(chip::EndpointId endpoint, uint16_t userIdx,
                         const EmberAfPluginDoorLockUserInfo & user);
    void UserPersistDelete(uint16_t userIdx);
    void CredPersistLoad(chip::EndpointId endpoint);
    void UserPersistLoad(chip::EndpointId endpoint);

private:
#if CONFIG_LOCK_HOST_MODE
    bool SendRequest(uint8_t ringCmd,
                     chip::EndpointId endpoint,
                     DlLockState targetState,
                     OperationSourceEnum opSource,
                     const Nullable<chip::FabricIndex> & fabricIdx,
                     const Nullable<chip::NodeId>      & nodeId,
                     OperationErrorEnum & err);
#endif /* CONFIG_LOCK_HOST_MODE */

    friend QcomLockManager & QcomLockMgr();
    static QcomLockManager sInstance;
};

inline QcomLockManager & QcomLockMgr()
{
    return QcomLockManager::sInstance;
}
