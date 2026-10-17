// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <queue>
#include <chrono>
#include <cstring>
#include "core/hle/service/friend/grid0_friends.h"
#include "common/logging.h"
#include "common/uuid.h"
#include "core/core.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/service/acc/errors.h"
#include "core/hle/service/friend/friend.h"
#include "core/hle/service/friend/friend_interface.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/server_manager.h"

namespace Service::Friend {

namespace {

// nn::friends::detail::FriendImpl.
constexpr size_t FriendRecordSize = 0x200;

u32 PresenceStatusOf(const Grid0::Friend& f) {
    if (f.state == "PLAYING") {
        return 2; // OnlinePlay
    }
    return f.state == "ONLINE" ? 1 : 0;
}

// Builds a friend record: uid 0x00, NSA id 0x10, nickname 0x18 (0x21 bytes), presence 0x40
// (uid, last-online 0x50, status 0x58, same-application 0x5C, app key-value storage 0x60..0x120),
// favourite 0x120, valid 0x128.
std::array<u8, FriendRecordSize> MakeFriendRecord(const Grid0::Friend& f, u64 current_title) {
    std::array<u8, FriendRecordSize> r{};
    const u64 uid_hi = 0x1100000000000000ULL;
    std::memcpy(r.data() + 0x00, &f.nsa_id, 8);
    std::memcpy(r.data() + 0x08, &uid_hi, 8);
    std::memcpy(r.data() + 0x10, &f.nsa_id, 8);
    std::memcpy(r.data() + 0x18, f.nickname.data(), std::min<size_t>(f.nickname.size(), 0x20));

    std::memcpy(r.data() + 0x40, r.data(), 0x10);
    const s64 last_online = 0x7FFFFFFFFFFFFFFFLL;
    std::memcpy(r.data() + 0x50, &last_online, 8);
    const u32 status = PresenceStatusOf(f);
    std::memcpy(r.data() + 0x58, &status, 4);
    // A friend in the same game is one the game asks about joining; one in another game is not.
    u64 friend_title = 0;
    try {
        friend_title = f.app_id.empty() ? 0 : std::stoull(f.app_id, nullptr, 16);
    } catch (...) {
    }
    r[0x5C] = status == 2 && friend_title != 0 && friend_title == current_title;
    std::memcpy(r.data() + 0x60, f.app_field.data(), std::min<size_t>(f.app_field.size(), 0xC0));

    r[0x120] = f.is_favorite;
    r[0x128] = 1;
    return r;
}

} // namespace

class IFriendService final : public ServiceFramework<IFriendService> {
public:
    explicit IFriendService(Core::System& system_)
        : ServiceFramework{system_, "IFriendService"}, service_context{system, "IFriendService"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IFriendService::GetCompletionEvent, "GetCompletionEvent"},
            {1, &IFriendService::Cancel, "Cancel"},
            {10100, &IFriendService::GetFriendListIds, "GetFriendListIds"},
            {10101, &IFriendService::GetFriendList, "GetFriendList"},
            {10102, &IFriendService::UpdateFriendInfo, "UpdateFriendInfo"},
            {10110, &IFriendService::GetFriendProfileImage, "GetFriendProfileImage"},
            {10111, nullptr, "GetFriendProfileImageWithImageSize"}, // 18.0.0+
            {10120, &IFriendService::CheckFriendListAvailability, "CheckFriendListAvailability"},
            {10121, nullptr, "EnsureFriendListAvailable"},
            {10200, nullptr, "SendFriendRequestForApplication"},
            {10211, nullptr, "AddFacedFriendRequestForApplication"},
            {10400, &IFriendService::GetBlockedUserListIds, "GetBlockedUserListIds"},
            {10420, &IFriendService::CheckBlockedUserListAvailability, "CheckBlockedUserListAvailability"},
            {10421, nullptr, "EnsureBlockedUserListAvailable"},
            {10500, nullptr, "GetProfileList"},
            {10501, nullptr, "GetProfileListV2"}, // 18.0.0+
            {10600, nullptr, "DeclareOpenOnlinePlaySession"},
            {10601, &IFriendService::DeclareCloseOnlinePlaySession, "DeclareCloseOnlinePlaySession"},
            {10610, &IFriendService::UpdateUserPresence, "UpdateUserPresence"},
            {10700, &IFriendService::GetPlayHistoryRegistrationKey, "GetPlayHistoryRegistrationKey"},
            {10701, nullptr, "GetPlayHistoryRegistrationKeyWithNetworkServiceAccountId"},
            {10702, nullptr, "AddPlayHistory"},
            {11000, nullptr, "GetProfileImageUrl"},
            {11001, nullptr, "GetProfileImageUrlV2"}, // 18.0.0+
            {20100, &IFriendService::GetFriendCount, "GetFriendCount"},
            {20101, &IFriendService::GetNewlyFriendCount, "GetNewlyFriendCount"},
            {20102, nullptr, "GetFriendDetailedInfo"},
            {20103, nullptr, "SyncFriendList"},
            {20104, &IFriendService::RequestSyncFriendList, "RequestSyncFriendList"},
            {20105, &IFriendService::GetFriendListForViewer, "GetFriendListForViewerV1"},
            {20106, nullptr, "UpdateFriendInfoForViewerV1"},
            {20107, nullptr, "GetFriendDetailedInfoV2"}, // 20.0.0+
            {20108, &IFriendService::GetFriendListForViewer, "GetFriendListForViewerV2"}, // 22.0.0+
            {20109, nullptr, "UpdateFriendInfoForViewerV2"}, // 22.0.0+
            {20110, nullptr, "LoadFriendSettingV1"},
            {20111, nullptr, "LoadFriendSettingV2"}, // 22.0.0+
            {20200, &IFriendService::GetReceivedFriendRequestCount, "GetReceivedFriendRequestCount"},
            {20201, nullptr, "GetFriendRequestListV1"},
            {20202, nullptr, "GetFriendRequestListV2"}, // 20.0.0+
            {20203, nullptr, "GetFriendRequestReceivedNotificationCount"}, // 22.0.0+
            {20300, nullptr, "GetFriendCandidateList"},
            {20301, nullptr, "GetNintendoNetworkIdInfo"},
            {20302, nullptr, "GetSnsAccountLinkage"}, // 5.0.0-19.0.1
            {20303, nullptr, "GetSnsAccountProfile"}, // 5.0.0-19.0.1
            {20304, nullptr, "GetSnsAccountFriendList"}, // 5.0.0-19.0.1
            {20400, nullptr, "GetBlockedUserListV1"},
            {20401, nullptr, "SyncBlockedUserList"},
            {20402, nullptr, "GetBlockedUserListV2"}, // 20.0.0+
            {20500, nullptr, "GetProfileExtraListV1"},
            {20501, nullptr, "GetRelationship"},
            {20502, nullptr, "GetProfileExtraListV2"}, // 19.0.0+
            {20600, &IFriendService::GetUserPresenceView, "GetUserPresenceViewV1"},
            {20601, &IFriendService::GetUserPresenceView, "GetUserPresenceViewV2"}, // 19.0.0+
            {20700, nullptr, "GetPlayHistoryListV1"},
            {20701, &IFriendService::GetPlayHistoryStatistics, "GetPlayHistoryStatistics"},
            {20702, nullptr, "GetPlayHistoryListV2"}, // 19.0.0+
            {20800, &IFriendService::LoadUserSetting, "LoadUserSettingV1"},
            {20801, nullptr, "SyncUserSetting"},
            {20802, &IFriendService::LoadUserSetting, "LoadUserSettingV2"}, // 19.0.0+
            {20900, &IFriendService::RequestListSummaryOverlayNotification, "RequestListSummaryOverlayNotification"},
            {21000, nullptr, "GetExternalApplicationCatalog"},
            {22000, nullptr, "GetReceivedFriendInvitationListV1"},
            {22001, nullptr, "GetReceivedFriendInvitationDetailedInfoV1"},
            {22002, nullptr, "GetReceivedFriendInvitationListV2"}, // 19.0.0+
            {22003, nullptr, "GetReceivedFriendInvitationDetailedInfoV2"}, // 19.0.0+
            {22010, &IFriendService::GetReceivedFriendInvitationCountCache, "GetReceivedFriendInvitationCountCache"},
            {30100, nullptr, "DropFriendNewlyFlags"},
            {30101, nullptr, "DeleteFriend"},
            {30110, nullptr, "DropFriendNewlyFlag"},
            {30120, nullptr, "ChangeFriendFavoriteFlag"},
            {30121, nullptr, "ChangeFriendOnlineNotificationFlag"},
            {30130, nullptr, "SetFriendNote"}, // 22.0.0+
            {30131, nullptr, "RequestUploadPendingNote"}, // 22.0.0+
            {30190, nullptr, "RequestSyncLocalUpdates"}, // 22.0.0+
            {30200, nullptr, "SendFriendRequest"},
            {30201, nullptr, "SendFriendRequestWithApplicationInfoV1"},
            {30202, nullptr, "CancelFriendRequest"},
            {30203, nullptr, "AcceptFriendRequest"},
            {30204, nullptr, "RejectFriendRequest"},
            {30205, nullptr, "ReadFriendRequest"},
            {30210, nullptr, "GetFacedFriendRequestRegistrationKey"},
            {30211, nullptr, "AddFacedFriendRequest"},
            {30212, nullptr, "CancelFacedFriendRequest"},
            {30213, nullptr, "GetFacedFriendRequestProfileImage"},
            {30214, nullptr, "GetFacedFriendRequestProfileImageFromPath"},
            {30215, nullptr, "SendFriendRequestWithExternalApplicationCatalogId"},
            {30216, nullptr, "ResendFacedFriendRequest"},
            {30217, nullptr, "SendFriendRequestWithNintendoNetworkIdInfo"},
            {30218, nullptr, "SendFriendRequestWithApplicationInfoV2"}, // 20.0.0+
            {30300, nullptr, "GetSnsAccountLinkPageUrl"}, // 5.0.0-19.0.1
            {30301, nullptr, "UnlinkSnsAccount"}, // 5.0.0-19.0.1
            {30400, nullptr, "BlockUser"},
            {30401, nullptr, "BlockUserWithApplicationInfoV1"},
            {30402, nullptr, "UnblockUser"},
            {30403, nullptr, "BlockUserWithApplicationInfoV2"}, // 20.0.0+
            {30500, nullptr, "GetProfileExtraFromFriendCodeV1"},
            {30501, nullptr, "GetProfileExtraFromFriendCodeV2"}, // 19.0.0+
            {30700, nullptr, "DeletePlayHistory"},
            {30701, nullptr, "AddPlayHistoryWithApplication"}, // 19.0.0+
            {30810, nullptr, "ChangePresencePermission"},
            {30811, nullptr, "ChangeFriendRequestReception"},
            {30812, nullptr, "ChangePlayLogPermission"},
            {30820, nullptr, "IssueFriendCode"},
            {30830, nullptr, "ClearPlayLog"},
            {30900, nullptr, "SendFriendInvitationV1"},
            {30901, nullptr, "SendFriendInvitationV2"}, // 19.0.0+
            {30910, nullptr, "ReadFriendInvitation"},
            {30911, nullptr, "ReadAllFriendInvitations"},
            {31000, nullptr, "OpenUser"}, // 19.0.0+
            {40100, nullptr, "DeleteFriendListCache"},
            {40400, nullptr, "DeleteBlockedUserListCache"},
            {49900, nullptr, "DeleteNetworkServiceAccountCache"},
        };
        // clang-format on

        RegisterHandlers(functions);

        completion_event = service_context.CreateEvent("IFriendService:CompletionEvent");
    }

    Result HandleSyncRequest(Kernel::KServerSession& session, HLERequestContext& context) override {
        const Result result = ServiceFrameworkBase::HandleSyncRequest(session, context);
        if (!is_async_session) {
            LOG_DEBUG(Service_Friend, "signalling completion after command {}", context.GetCommand());
            completion_event->Signal(system.Kernel());
        }
        ++handled_requests;
        return result;
    }

    ~IFriendService() override {
        service_context.CloseEvent(completion_event);
    }

private:
    void GetFriendProfileImage(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    [[maybe_unused]] const auto uuid = rp.PopRaw<Common::UUID>();
    const auto friend_id = rp.Pop<u64>();
    u32 size = 0;
    // The game does not ask again: a short wait beats a "?" picture for the whole session.
    if (const auto jpeg = Grid0::ProfileImage(friend_id, std::chrono::milliseconds(1500));
        jpeg && !jpeg->empty() && jpeg->size() <= ctx.GetWriteBufferSize()) {
        ctx.WriteBuffer(*jpeg);
        size = static_cast<u32>(jpeg->size());
    }
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(size);
}

    void UpdateFriendInfo(HLERequestContext& ctx) {
    // One record per requested id, in the same order: the caller pairs answers with its ids by
    // position, so an id we cannot resolve stays an invalid record rather than being skipped.
    const auto input = ctx.ReadBuffer();
    std::vector<u64> wanted(input.size() / sizeof(u64));
    std::memcpy(wanted.data(), input.data(), wanted.size() * sizeof(u64));

    const auto friends = Grid0::Get();
    const u64 title = system.GetApplicationProcessProgramID();
    const size_t capacity = ctx.GetWriteBufferSize() / FriendRecordSize;
    std::vector<u8> out;
    size_t resolved = 0;
    for (size_t i = 0; i < wanted.size() && i < capacity; ++i) {
        std::array<u8, FriendRecordSize> record{};
        for (const auto& f : friends) {
            if (f.nsa_id == wanted[i]) {
                record = MakeFriendRecord(f, title);
                ++resolved;
                break;
            }
        }
        out.insert(out.end(), record.begin(), record.end());
    }
    if (!out.empty()) {
        ctx.WriteBuffer(out);
    }
    LOG_DEBUG(Service_Friend, "UpdateFriendInfo {} id(s) -> {} resolved", wanted.size(), resolved);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

    void GetFriendListIds(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto offset = rp.Pop<u32>();
        // A game asks once, early (Splatoon 3 at boot), and keeps what it got for the whole
        // session; a short, bounded wait for the first fetch beats an empty list forever.
        const auto friends = Grid0::GetWarm(std::chrono::milliseconds(2000));
        const size_t capacity = ctx.GetWriteBufferNumElements<u64>();
        std::vector<u64> ids;
        for (size_t i = offset; i < friends.size() && ids.size() < capacity; ++i) {
            ids.push_back(friends[i].nsa_id);
        }
        if (!ids.empty()) {
            ctx.WriteBuffer(ids);
        }
        LOG_INFO(Service_Friend, "GetFriendListIds offset={} -> {}", offset, ids.size());
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(static_cast<u32>(ids.size()));
    }

    enum class PresenceFilter : u32 {
        None = 0,
        Online = 1,
        OnlinePlay = 2,
        OnlineOrOnlinePlay = 3,
    };

    struct SizedFriendFilter {
        PresenceFilter presence;
        u8 is_favorite;
        u8 same_app;
        u8 same_app_played;
        u8 arbitrary_app_played;
        u64 group_id;
    };
    static_assert(sizeof(SizedFriendFilter) == 0x10, "SizedFriendFilter is an invalid size");

    struct FriendsUserSetting {
        Common::UUID uuid;
        u32 presence_permission;
        u32 play_log_permission;
        u64 friend_request_reception;
        char friend_code[0x20];
        u64 friend_code_next_issuable_time;
        u8 unk_x48[0x7B8];
    };
    static_assert(sizeof(FriendsUserSetting) == 0x800, "FriendsUserSetting is an invalid size");

    void GetCompletionEvent(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "GetCompletionEvent called");
        // AsyncContextInternal opens a fresh session and asks for this first.
        if (handled_requests == 0) {
            is_async_session = true;
        }
        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(ctx, completion_event->GetReadableEvent());
    }

    void Cancel(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "(STUBBED) called.");

        // TODO (jarrodnorwell)

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetFriendList(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto offset = rp.Pop<u32>();
    const auto friends = Grid0::Get();
    const u64 title = system.GetApplicationProcessProgramID();
    const size_t capacity = ctx.GetWriteBufferSize() / FriendRecordSize;
    std::vector<u8> out;
    u32 count = 0;
    for (size_t i = offset; i < friends.size() && count < capacity; ++i, ++count) {
        const auto record = MakeFriendRecord(friends[i], title);
        out.insert(out.end(), record.begin(), record.end());
    }
    if (!out.empty()) {
        ctx.WriteBuffer(out);
    }
    LOG_INFO(Service_Friend, "GetFriendList offset={} -> {}", offset, count);
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(count);
}

    void CheckFriendListAvailability(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto uuid{rp.PopRaw<Common::UUID>()};

        LOG_WARNING(Service_Friend, "(STUBBED) called, uuid=0x{}", uuid.RawString());

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(true);
    }

    void GetBlockedUserListIds(HLERequestContext& ctx) {
        // This is safe to stub, as there should be no adverse consequences from reporting no
        // blocked users.
        LOG_WARNING(Service_Friend, "(STUBBED) called");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0); // Indicates there are no blocked users
    }

    void CheckBlockedUserListAvailability(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto uuid{rp.PopRaw<Common::UUID>()};

        LOG_WARNING(Service_Friend, "(STUBBED) called, uuid=0x{}", uuid.RawString());

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(true);
    }

    void DeclareCloseOnlinePlaySession(HLERequestContext& ctx) {
        // Stub used by Splatoon 2
        LOG_WARNING(Service_Friend, "(STUBBED) called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void UpdateUserPresence(HLERequestContext& ctx) {
    // The nn::friends UserPresence (0xE0): uid 0x00, last-online 0x10, status 0x18, the game's
    // app key-value storage 0x20..0xE0. The status and that blob are what a friend needs to see
    // this player online and join them.
    const auto presence = ctx.ReadBuffer();
    if (presence.size() >= 0xE0) {
        u32 status{};
        std::memcpy(&status, presence.data() + 0x18, sizeof(status));
        const std::vector<u8> app_field(presence.begin() + 0x20, presence.begin() + 0xE0);
        Grid0::PublishPresence(status, system.GetApplicationProcessProgramID(), app_field);
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

    void GetPlayHistoryRegistrationKey(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto local_play = rp.Pop<bool>();
        const auto uuid = rp.PopRaw<Common::UUID>();

        LOG_WARNING(Service_Friend, "(STUBBED) called, local_play={}, uuid=0x{}", local_play,
                    uuid.RawString());

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetFriendCount(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(static_cast<s32>(Grid0::Get().size()));
}

    void GetNewlyFriendCount(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(0);
    }

    void RequestSyncFriendList(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "(STUBBED) called.");

        // TODO (jarrodnorwell)

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetFriendListForViewer(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0);
    }

    void GetReceivedFriendRequestCount(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        [[maybe_unused]] const auto uuid = rp.PopRaw<Common::UUID>();

        LOG_DEBUG(Service_Friend, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push(0);
        rb.Push(0);
    }

    void GetUserPresenceView(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto uuid = rp.PopRaw<Common::UUID>();
        LOG_DEBUG(Service_Friend, "(STUBBED) called, uuid={}.", uuid.RawString());

        u8 buf[0xe0]{};
        ctx.WriteBuffer(buf);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
    }

    void GetPlayHistoryStatistics(HLERequestContext& ctx) {
        LOG_ERROR(Service_Friend, "(STUBBED) called, check in out");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void LoadUserSetting(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto uuid = rp.PopRaw<Common::UUID>();

        LOG_WARNING(Service_Friend, "(STUBBED) called");

        FriendsUserSetting setting{};
        setting.uuid = uuid;
        setting.presence_permission = 2;
        setting.play_log_permission = 5;
        setting.friend_request_reception = 1;
        setting.friend_code_next_issuable_time = 99999999999;
        strcpy(setting.friend_code, "0000-0000-0000");
        ctx.WriteBuffer(setting);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void RequestListSummaryOverlayNotification(HLERequestContext& ctx) {
        LOG_INFO(Service_Friend, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetReceivedFriendInvitationCountCache(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "(STUBBED) called, check in out");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(0);
    }

    KernelHelpers::ServiceContext service_context;

    Kernel::KEvent* completion_event;
    u64 handled_requests{};
    bool is_async_session{};
};

class INotificationService final : public ServiceFramework<INotificationService> {
public:
    explicit INotificationService(Core::System& system_, Common::UUID uuid_)
        : ServiceFramework{system_, "INotificationService"}, uuid{uuid_},
          service_context{system_, "INotificationService"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &INotificationService::GetEvent, "GetEvent"},
            {1, &INotificationService::Clear, "Clear"},
            {2, &INotificationService::Pop, "Pop"}
        };
        // clang-format on

        RegisterHandlers(functions);

        notification_event = service_context.CreateEvent("INotificationService:NotifyEvent");
    }

    ~INotificationService() override {
        service_context.CloseEvent(notification_event);
    }

private:
    void GetEvent(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "called");

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(ctx, notification_event->GetReadableEvent());
    }

    void Clear(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "called");
        while (!notifications.empty()) {
            notifications.pop();
        }
        std::memset(&states, 0, sizeof(States));

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Pop(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "called");

        if (notifications.empty()) {
            LOG_ERROR(Service_Friend, "No notifications in queue!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Account::ResultNoNotifications);
            return;
        }

        const auto notification = notifications.front();
        notifications.pop();

        switch (notification.notification_type) {
        case NotificationTypes::HasUpdatedFriendsList:
            states.has_updated_friends = false;
            break;
        case NotificationTypes::HasReceivedFriendRequest:
            states.has_received_friend_request = false;
            break;
        default:
            // HOS seems not have an error case for an unknown notification
            LOG_WARNING(Service_Friend, "Unknown notification {:08X}",
                        notification.notification_type);
            break;
        }

        IPC::ResponseBuilder rb{ctx, 6};
        rb.Push(ResultSuccess);
        rb.PushRaw<SizedNotificationInfo>(notification);
    }

    enum class NotificationTypes : u32 {
        HasUpdatedFriendsList = 0x65,
        HasReceivedFriendRequest = 0x1
    };

    struct SizedNotificationInfo {
        NotificationTypes notification_type;
        INSERT_PADDING_WORDS(
            1); // TODO(ogniK): This doesn't seem to be used within any IPC returns as of now
        u64_le account_id;
    };
    static_assert(sizeof(SizedNotificationInfo) == 0x10,
                  "SizedNotificationInfo is an incorrect size");

    struct States {
        bool has_updated_friends;
        bool has_received_friend_request;
    };

    Common::UUID uuid;
    KernelHelpers::ServiceContext service_context;

    Kernel::KEvent* notification_event;
    std::queue<SizedNotificationInfo> notifications;
    States states{};
};

void Module::Interface::CreateFriendService(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IFriendService>(ctx, system);
    LOG_DEBUG(Service_Friend, "called");
}

void Module::Interface::CreateNotificationService(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    auto uuid = rp.PopRaw<Common::UUID>();

    LOG_DEBUG(Service_Friend, "called, uuid=0x{}", uuid.RawString());

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<INotificationService>(ctx, system, uuid);
}

Module::Interface::Interface(std::shared_ptr<Module> module_, Core::System& system_, const char* name)
    : ServiceFramework{system_, name}, module{std::move(module_)}
{}

Module::Interface::~Interface() = default;

class IServiceForApplication final : public ServiceFramework<IServiceForApplication> {
public:
    explicit IServiceForApplication(Core::System& system_)
        : ServiceFramework{system_, "nd:app"}
    {
        static const FunctionInfo functions[] = {
            {0, nullptr, "GetReceivableNeighborInfoCountMax"},
            {10, nullptr, "IsNeighborDetectionEnabled"},
        };
        RegisterHandlers(functions);
    }
};

class IServiceForSystem final : public ServiceFramework<IServiceForSystem> {
public:
    explicit IServiceForSystem(Core::System& system_)
        : ServiceFramework{system_, "nd:sys"}
    {
        static const FunctionInfo functions[] = {
            {0, nullptr, "GetReceivableNeighborInfoCountMax"},
            {10, nullptr, "IsNeighborDetectionEnabled"},
            {200, nullptr, "SetSystemData"},
            {201, nullptr, "ClearSystemData"},
            {203, nullptr, "GetReceivableNeighborInfoCountForSystem"},
            {204, nullptr, "ReceiveNeighborInfoForSystem"},
            {205, nullptr, "SetSender"},
            {206, nullptr, "GetSender"},
            {207, nullptr, "CreateScannerForSystem"},
            {208, nullptr, "CreateReceiveEventHolderForSystem"},
            {223, nullptr, "EnableNeighborDetection"},
            {224, nullptr, "DisableNeighborDetection"},
            {226, nullptr, "EnablePowerSave"},
            {227, nullptr, "DisablePowerSave"},
            {228, nullptr, "IsPowerSaveEnabled"},
            {232, nullptr, "ClearBlockedUsers"},
            {233, nullptr, "GetBlockedUserCount"},
            {234, nullptr, "BlockUserByLocalUserId"},
            {235, nullptr, "BlockUserByNetworkUserId"},
            {236, nullptr, "UnblockUserByLocalUserId"},
            {237, nullptr, "UnblockUserByNetworkUserId"},
            {240, nullptr, "DeleteApplication"},
            {250, nullptr, "InitializeApplicationInfo"},
            {260, nullptr, "CreateAccountSystemSaveDataAccessSuppressor"},
            {300, nullptr, "AddReceivedNeighborInfoForSystemForDebug"},
            {301, nullptr, "GetSendDataForDebug"},
            {302, nullptr, "ClearReceiveCounterForDebug"},
            {303, nullptr, "GetNextReceiveCounterForDebug"},
            {304, nullptr, "ListBlockedUsersForDebug"},
            {305, nullptr, "RefreshSendDataIdForDebug"},
            {306, nullptr, "ReloadFwdbgSettingsForDebug"},
            {307, nullptr, "EnableApplicationForDebug"},
            {308, nullptr, "GetNextReceiveCountersForDebug"},
            {309, nullptr, "ListApplicationInfoForDebug"},
            {310, nullptr, "SetApplicationDataForDebug"},
            {400, nullptr, "GetNetworkUserId"},
            {401, nullptr, "DeleteNetworkUserId"},
        };
        RegisterHandlers(functions);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);
    auto module = std::make_shared<Module>();

    server_manager->RegisterNamedService("nd:app", std::make_shared<IServiceForApplication>(system));
    server_manager->RegisterNamedService("nd:sys", std::make_shared<IServiceForSystem>(system));

    server_manager->RegisterNamedService("friend:a", std::make_shared<Friend>(module, system, "friend:a"));
    server_manager->RegisterNamedService("friend:m", std::make_shared<Friend>(module, system, "friend:m"));
    server_manager->RegisterNamedService("friend:s", std::make_shared<Friend>(module, system, "friend:s"));
    server_manager->RegisterNamedService("friend:u", std::make_shared<Friend>(module, system, "friend:u"));
    server_manager->RegisterNamedService("friend:v", std::make_shared<Friend>(module, system, "friend:v"));

    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::Friend
