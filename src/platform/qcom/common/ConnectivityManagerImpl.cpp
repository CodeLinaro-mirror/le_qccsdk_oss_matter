/*
 *    Copyright (c) 2024 Project CHIP Authors
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

/**
 *    @file
 *          ConnectivityManager implementation for QCC730 (Zephyr/WiFi-only).
 *
 *          QCC730 has no BLE and no Thread, so those generic template
 *          instantiations are omitted.  WiFi is handled by the Zephyr
 *          platform layer (ConnectivityManagerImplWiFi / WiFiManager)
 *          which listens to standard Zephyr net_mgmt events raised by
 *          the qcom_wifi_drv driver.
 */

#include <platform/internal/CHIPDeviceLayerInternal.h>

#include <inet/UDPEndPointImplSockets.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/ConnectivityManager.h>
#include <platform/Zephyr/InetUtils.h>

#include <zephyr/net/net_if.h>

#if CHIP_DEVICE_CONFIG_ENABLE_WIFIPAF
#include <platform/CommissionableDataProvider.h>
#include <platform/DeviceInstanceInfoProvider.h>
extern "C" {
#include <qcc730_nan_de_glue.h>  // nogncheck — Zephyr CMake -isystem, no GN target; extern "C" required (no guard)
#include <qcom_wifi_mgmt.h>      // nogncheck — Zephyr CMake -isystem, no GN target; extern "C" required (no guard)
}
#include <wifipaf/WiFiPAFLayer.h>
#endif

#include <platform/internal/GenericConnectivityManagerImpl_UDP.ipp>

#if INET_CONFIG_ENABLE_TCP_ENDPOINT
#include <platform/internal/GenericConnectivityManagerImpl_TCP.ipp>
#endif

// QCC730 has no BLE and no Thread — do not include those .ipp files.

using namespace ::chip::Inet;
using namespace ::chip::DeviceLayer::Internal;

namespace chip {
namespace DeviceLayer {

namespace {

/**
 * Helper: join or leave an IPv6 multicast group on a Zephyr net_if.
 * Used by the UDP multicast group handler below.
 */
static CHIP_ERROR JoinLeaveMulticastGroup(net_if * iface, const Inet::IPAddress & address,
                                          UDPEndPointImplSockets::MulticastOperation operation)
{
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    const in6_addr in6Addr = InetUtils::ToZephyrAddr(address);

    if (operation == UDPEndPointImplSockets::MulticastOperation::kJoin)
    {
        net_if_mcast_addr * maddr = net_if_ipv6_maddr_add(iface, &in6Addr);
        if (maddr && !net_if_ipv6_maddr_is_joined(maddr))
        {
            net_if_ipv6_maddr_join(iface, maddr);
        }
    }
    else if (operation == UDPEndPointImplSockets::MulticastOperation::kLeave)
    {
        VerifyOrReturnError(net_if_ipv6_maddr_rm(iface, &in6Addr), CHIP_ERROR_INVALID_ADDRESS);
    }
    else
    {
        return CHIP_ERROR_INCORRECT_STATE;
    }
#endif // CHIP_DEVICE_CONFIG_ENABLE_WIFI

    return CHIP_NO_ERROR;
}

} // namespace

ConnectivityManagerImpl ConnectivityManagerImpl::sInstance;

CHIP_ERROR ConnectivityManagerImpl::_Init()
{
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    // Initialise the Zephyr WiFi platform layer (registers net_mgmt callbacks,
    // sets up the WiFiManager state machine, etc.).
    ReturnErrorOnFailure(InitWiFi());
#endif

    // Register the multicast group handler so that Matter can join/leave
    // IPv6 multicast groups on the WiFi interface.
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    UDPEndPointImplSockets::SetMulticastGroupHandler(
        [](InterfaceId interfaceId, const IPAddress & address,
           UDPEndPointImplSockets::MulticastOperation operation) -> CHIP_ERROR {
            if (interfaceId.IsPresent())
            {
                net_if * iface = InetUtils::GetInterface(interfaceId);
                VerifyOrReturnError(iface != nullptr, INET_ERROR_UNKNOWN_INTERFACE);
                return JoinLeaveMulticastGroup(iface, address, operation);
            }

            // No specific interface: apply to all interfaces.
            for (int i = 1; net_if * iface = net_if_get_by_index(i); i++)
            {
                ReturnErrorOnFailure(JoinLeaveMulticastGroup(iface, address, operation));
            }
            return CHIP_NO_ERROR;
        });
#endif // CHIP_DEVICE_CONFIG_ENABLE_WIFI

    return CHIP_NO_ERROR;
}

void ConnectivityManagerImpl::_OnPlatformEvent(const ChipDeviceEvent * event)
{
#if CHIP_DEVICE_CONFIG_ENABLE_WIFIPAF
    WiFiPAF::WiFiPAFLayer & wifiPafLayer = WiFiPAF::WiFiPAFLayer::GetWiFiPAFLayer();

    switch (event->Type)
    {
    case DeviceEventType::kCHIPoWiFiPAFReceived: {
        WiFiPAF::WiFiPAFSession rxInfo;
        memcpy(&rxInfo, &event->CHIPoWiFiPAFReceived.SessionInfo, sizeof(rxInfo));

        // Publisher side: WiFiPAFEndPoint must exist before OnWiFiPAFMessageReceived
        // can find it (Find matches id + peer_id + peer_addr).  Create it on the
        // first connection attempt with the full peer info from the incoming message.
        if (!mWiFiPAFEndpointCreated)
        {
            rxInfo.role = WiFiPAF::kWiFiPafRole_Publisher;
            CHIP_ERROR initErr = wifiPafLayer.HandleTransportConnectionInitiated(
                rxInfo, nullptr, nullptr, nullptr);
            if (initErr == CHIP_NO_ERROR)
            {
                ChipLogProgress(DeviceLayer, "WiFiPAF: endpoint created for publisher (peer_id=%u)",
                       (unsigned) rxInfo.peer_id);
                mWiFiPAFEndpointCreated = true;
            }
            else
            {
                ChipLogError(DeviceLayer, "WiFiPAF: endpoint creation failed err=%d", (int) initErr.AsInteger());
            }
        }

        wifiPafLayer.OnWiFiPAFMessageReceived(rxInfo,
            chip::System::PacketBufferHandle::Adopt(event->CHIPoWiFiPAFReceived.Data));
        break;
    }
    case DeviceEventType::kCHIPoWiFiPAFConnected: {
        // PASE/CASE connection established.  QCC730 is publisher-only and has
        // no subscriber completion callbacks; WiFiPAFLayer handles internally.
        ChipLogProgress(DeviceLayer, "WiFi-PAF: event: kCHIPoWiFiPAFConnected");
        break;
    }
    case DeviceEventType::kCHIPoWiFiPAFCancelConnect: {
        // Connection cancelled.  QCC730 is publisher-only; nothing to notify.
        ChipLogProgress(DeviceLayer, "WiFi-PAF: event: kCHIPoWiFiPAFCancelConnect");
        break;
    }
    case DeviceEventType::kCHIPoWiFiPAFWriteDone: {
        ChipLogProgress(DeviceLayer, "WiFi-PAF: event: kCHIPoWiFiPAFWriteDone");
        WiFiPAF::WiFiPAFSession txInfo;
        memcpy(&txInfo, &event->CHIPoWiFiPAFReceived.SessionInfo, sizeof(txInfo));
        wifiPafLayer.HandleWriteConfirmed(txInfo, event->CHIPoWiFiPAFReceived.result);
        break;
    }
    default:
        break;
    }
#else
    (void) event;
#endif
}

} // namespace DeviceLayer
} // namespace chip

// -- WiFi-PAF (NAN USD) platform layer ------------------------------------------
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI && CHIP_DEVICE_CONFIG_ENABLE_WIFIPAF

namespace {

// NAN service name for Matter commissioning (same as Linux reference impl).
constexpr char kNanServiceName[]    = "_matterc._udp";
// Matter CSA service protocol type (enum from qcom_wifi_mgmt.h).
constexpr uint8_t kNanSrvProtoMatter = 3;

// SSI payload written into every NAN Publish SDF.
// Must be packed so the byte layout matches the commissioner's parser exactly.
#pragma pack(push, 1)
struct PAFPublishSSI
{
    uint8_t  DevOpCode;       ///< always 0
    uint16_t DevInfo;         ///< setup discriminator (11-bit, stored LE)
    uint16_t ProductId;
    uint16_t VendorId;
};
#pragma pack(pop)

} // anonymous namespace

namespace chip {
namespace DeviceLayer {

CHIP_ERROR ConnectivityManagerImpl::_WiFiPAFPublish(
    ConnectivityManager::WiFiPAFAdvertiseParam & InArgs)
{
    // Initialise the WiFiPAF protocol layer (idempotent after first call).
    WiFiPAF::WiFiPAFLayer & wifiPafLayer = WiFiPAF::WiFiPAFLayer::GetWiFiPAFLayer();
    ReturnErrorOnFailure(wifiPafLayer.Init(&DeviceLayer::SystemLayer()));

    // Register the glue-layer receive callback so incoming NAN follow-ups
    // are forwarded into the Matter event loop.
    qcc730_nan_glue_set_receive_cb(OnNanReceiveCallback);

    // Build the SSI that carries discriminator / product / vendor IDs.
    PAFPublishSSI ssi = {};
    ssi.DevOpCode = 0;
    VerifyOrDie(DeviceLayer::GetCommissionableDataProvider()
                    ->GetSetupDiscriminator(ssi.DevInfo) == CHIP_NO_ERROR);
    if (DeviceLayer::GetDeviceInstanceInfoProvider()
            ->GetProductId(ssi.ProductId) != CHIP_NO_ERROR)
    {
        ssi.ProductId = 0;
    }
    if (DeviceLayer::GetDeviceInstanceInfoProvider()
            ->GetVendorId(ssi.VendorId) != CHIP_NO_ERROR)
    {
        ssi.VendorId = 0;
    }

    // Publish on the channel set by the commissioner via SetApFreq(), or
    // fall back to 2.4 GHz CH6 (2437 MHz) — the NAN social channel QCC730
    // always uses for commissioning.
    // Note: freq_list is stack-allocated; net_mgmt is synchronous so the
    // pointer is valid for the entire call duration.
    int freq_list[2] = {
        (mApFreq != 0) ? (int) mApFreq : CHIP_DEVICE_CONFIG_WIFIPAF_24G_DEFAUTL_CHNL,
        0 // NULL-terminated
    };

    struct qcom_wifi_nan_publish_params params = {
        .service_name   = kNanServiceName,
        .srv_proto_type = kNanSrvProtoMatter,
        .freq_list      = freq_list,
        .ssi            = reinterpret_cast<const uint8_t *>(&ssi),
        .ssi_len        = sizeof(ssi),
        .ttl            = CHIP_DEVICE_CONFIG_WIFIPAF_MAX_ADVERTISING_TIMEOUT_SECS,
        .unsolicited    = true,  // broadcast Publish SDF periodically
        .solicited      = true,  // also reply to active Subscribe SDFs
    };

    struct net_if * iface = net_if_get_wifi_sta();
    VerifyOrReturnError(iface != nullptr, CHIP_ERROR_INCORRECT_STATE);

    int publish_id = net_mgmt(NET_REQUEST_WIFI_QCOM_NAN_PUBLISH,
                              iface, &params, sizeof(params));
    VerifyOrReturnError(publish_id > 0, CHIP_ERROR_INTERNAL,
        ChipLogError(DeviceLayer, "WiFi-PAF: NAN publish failed (ret=%d)", publish_id));

    ChipLogProgress(DeviceLayer, "WiFi-PAF: publish started, id=%d", publish_id);

    WiFiPAF::WiFiPAFSession sessionInfo = {
        .role = WiFiPAF::kWiFiPafRole_Publisher,
        .id   = static_cast<uint32_t>(publish_id),
    };
    ReturnErrorOnFailure(
        wifiPafLayer.AddPafSession(WiFiPAF::PafInfoAccess::kAccSessionId, sessionInfo));

    InArgs.publish_id = static_cast<uint32_t>(publish_id);
    mPublishId = InArgs.publish_id;
    return CHIP_NO_ERROR;
}

CHIP_ERROR ConnectivityManagerImpl::_WiFiPAFCancelPublish(uint32_t PublishId)
{
    struct qcom_wifi_nan_cancel_publish_params params = {
        .publish_id = static_cast<int>(PublishId),
    };
    struct net_if * iface = net_if_get_wifi_sta();
    VerifyOrReturnError(iface != nullptr, CHIP_ERROR_INCORRECT_STATE);

    net_mgmt(NET_REQUEST_WIFI_QCOM_NAN_CANCEL_PUBLISH, iface, &params, sizeof(params));
    ChipLogProgress(DeviceLayer, "WiFi-PAF: cancel publish id=%u", (unsigned) PublishId);
    return CHIP_NO_ERROR;
}

// ── Subscriber side — QCC730 is a commissionee (Publisher only) ──────────
CHIP_ERROR ConnectivityManagerImpl::_WiFiPAFSubscribe(
    const uint16_t & /*connDiscriminator*/, void * /*appState*/,
    OnConnectionCompleteFunct /*onSuccess*/, OnConnectionErrorFunct /*onError*/)
{
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

CHIP_ERROR ConnectivityManagerImpl::_WiFiPAFCancelSubscribe(uint32_t /*SubscribeId*/)
{
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

CHIP_ERROR ConnectivityManagerImpl::_WiFiPAFCancelIncompleteSubscribe()
{
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

// ── TX path ──────────────────────────────────────────────────────────────
CHIP_ERROR ConnectivityManagerImpl::_WiFiPAFSend(
    const WiFiPAF::WiFiPAFSession & TxInfo,
    chip::System::PacketBufferHandle && msgBuf)
{
    VerifyOrReturnError(!msgBuf.IsNull(), CHIP_ERROR_INVALID_ARGUMENT);

    // CompactHead if chained so the payload is contiguous.
    if (msgBuf->HasChainedBuffer())
    {
        msgBuf->CompactHead();
        if (msgBuf->HasChainedBuffer())
        {
            ChipLogError(DeviceLayer, "WiFi-PAF: outbound message too large");
            return CHIP_ERROR_OUTBOUND_MESSAGE_TOO_BIG;
        }
    }

    struct qcom_wifi_nan_transmit_params params = {
        .handle          = static_cast<int>(TxInfo.id),
        .req_instance_id = static_cast<uint8_t>(TxInfo.peer_id),
        .ssi             = msgBuf->Start(),
        .ssi_len         = msgBuf->DataLength(),
    };
    memcpy(params.peer_addr, TxInfo.peer_addr, sizeof(params.peer_addr));

    struct net_if * iface = net_if_get_wifi_sta();
    VerifyOrReturnError(iface != nullptr, CHIP_ERROR_INCORRECT_STATE);

    int ret = net_mgmt(NET_REQUEST_WIFI_QCOM_NAN_TRANSMIT,
                       iface, &params, sizeof(params));

    // Notify the WiFiPAF layer that the TX is done (required for PASE state
    // advance).  This must be posted even on failure (result = false) so the
    // layer can perform error handling; omitting it would stall the state machine.
    ChipDeviceEvent event;
    event.Type = DeviceEventType::kCHIPoWiFiPAFWriteDone;
    memcpy(&event.CHIPoWiFiPAFReceived.SessionInfo, &TxInfo, sizeof(TxInfo));
    event.CHIPoWiFiPAFReceived.result = (ret == 0);
    PlatformMgr().PostEventOrDie(&event);

    return (ret == 0) ? CHIP_NO_ERROR : CHIP_ERROR_INTERNAL;
}

CHIP_ERROR ConnectivityManagerImpl::_WiFiPAFShutdown(uint32_t id,
                                                       WiFiPAF::WiFiPafRole role)
{
    if (role == WiFiPAF::kWiFiPafRole_Publisher)
    {
        ReturnErrorOnFailure(_WiFiPAFCancelPublish(id));
    }
    // Unregister the receive callback — no more NAN frames should be forwarded.
    qcc730_nan_glue_set_receive_cb(nullptr);
    // Reset endpoint flag so the next commissioning cycle creates a fresh endpoint.
    mWiFiPAFEndpointCreated = false;
    mPublishId = 0;
    ChipLogProgress(DeviceLayer, "WiFi-PAF: shutdown id=%u", (unsigned) id);
    return CHIP_NO_ERROR;
}

void ConnectivityManagerImpl::WiFiPafCancelActivePublish()
{
    if (mPublishId != 0)
    {
        _WiFiPAFCancelPublish(mPublishId);
        mPublishId = 0;
    }
}

// ── NAN receive callback (called from eloop thread) ──────────────────────
void ConnectivityManagerImpl::OnNanReceiveCallback(
    int id, int peer_instance_id,
    const uint8_t * ssi, size_t ssi_len,
    const uint8_t * peer_addr)
{

    // Allocate a PacketBuffer for the SSI payload.
    chip::System::PacketBufferHandle buf =
        chip::System::PacketBufferHandle::NewWithData(ssi, ssi_len);
    if (buf.IsNull())
    {
        ChipLogError(DeviceLayer, "WiFiPAF: OOM in OnNanReceiveCallback");
        return;
    }

    WiFiPAF::WiFiPAFSession rxSession = {};
    rxSession.id      = static_cast<uint32_t>(id);
    rxSession.peer_id = static_cast<uint32_t>(peer_instance_id);
    memcpy(rxSession.peer_addr, peer_addr, sizeof(rxSession.peer_addr));
    // nodeId / discriminator are not known from the NAN layer;
    // WiFiPAFLayer will fill them in from the PASE exchange.

    ChipLogProgress(DeviceLayer,
        "WiFi-PAF: OnNanReceive id=%d peer_id=%d ssi_len=%u",
        id, peer_instance_id, (unsigned) ssi_len);

    // Cross into the Matter thread before touching any Matter APIs.
    ChipDeviceEvent event;
    event.Type = DeviceEventType::kCHIPoWiFiPAFReceived;
    event.CHIPoWiFiPAFReceived.Data = std::move(buf).UnsafeRelease();
    memcpy(&event.CHIPoWiFiPAFReceived.SessionInfo, &rxSession, sizeof(rxSession));
    event.CHIPoWiFiPAFReceived.result = true;
    PlatformMgr().PostEventOrDie(&event);
}

} // namespace DeviceLayer
} // namespace chip

#endif // CHIP_DEVICE_CONFIG_ENABLE_WIFI && CHIP_DEVICE_CONFIG_ENABLE_WIFIPAF