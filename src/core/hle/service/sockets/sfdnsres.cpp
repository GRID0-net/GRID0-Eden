// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <string_view>
#include <utility>
#include <vector>

#include "common/string_util.h"
#include "common/swap.h"
#include "core/core.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/sockets/private_server.h"
#include "core/hle/service/sockets/nsd.h"
#include "core/hle/service/sockets/deferred_poll_waker.h"
#include "core/hle/service/sockets/sfdnsres.h"
#include "core/hle/service/sockets/sockets.h"
#include "core/hle/service/sockets/sockets_translate.h"
#include "core/internal_network/network.h"
#include "core/memory.h"

namespace Service::Sockets {

SFDNSRES::SFDNSRES(Core::System& system_) : ServiceFramework{system_, "sfdnsres"} {
    static const FunctionInfo functions[] = {
        {0, nullptr, "SetDnsAddressesPrivateRequest"},
        {1, nullptr, "GetDnsAddressPrivateRequest"},
        {2, &SFDNSRES::GetHostByNameRequest, "GetHostByNameRequest"},
        {3, nullptr, "GetHostByAddrRequest"},
        {4, nullptr, "GetHostStringErrorRequest"},
        {5, &SFDNSRES::GetGaiStringErrorRequest, "GetGaiStringErrorRequest"},
        {6, &SFDNSRES::GetAddrInfoRequest, "GetAddrInfoRequest"},
        {7, nullptr, "GetNameInfoRequest"},
        {8, nullptr, "RequestCancelHandleRequest"},
        {9, nullptr, "CancelRequest"},
        {10, &SFDNSRES::GetHostByNameRequestWithOptions, "GetHostByNameRequestWithOptions"},
        {11, nullptr, "GetHostByAddrRequestWithOptions"},
        {12, &SFDNSRES::GetAddrInfoRequestWithOptions, "GetAddrInfoRequestWithOptions"},
        {13, nullptr, "GetNameInfoRequestWithOptions"},
        {14, &SFDNSRES::ResolverSetOptionRequest, "ResolverSetOptionRequest"},
        {15, nullptr, "ResolverGetOptionRequest"},
    };
    RegisterHandlers(functions);
}

SFDNSRES::~SFDNSRES() = default;

enum class NetDbError : s32 {
    Internal = -1,
    Success = 0,
    HostNotFound = 1,
    TryAgain = 2,
    NoRecovery = 3,
    NoData = 4,
};

static std::string RequestedHost(HLERequestContext& ctx, bool use_nsd) {
    auto host = Common::StringFromBuffer(ctx.ReadBuffer(0));
    return use_nsd || host.find('%') != std::string::npos ? ResolveNsdFqdn(host) : host;
}

static const constexpr std::array blockedDomains = {
    "srv.nintendo.net", //obvious
    "nintendo.es",
    "nintendowifi.net",
    "nintendo-europe.com",
    "nintendo.com.hk",
    "nintendo.com.au",
    "nintendo.co.kr",
    "nintendo.co.uk",
    "nintendo.co.jp",
    "nintendo.co.nz",
    "nintendo.co.za",
    "nintendo.com",
    "nintendo.jp",
    "nintendo.tw",
    "nintendo.at",
    "nintendo.be",
    "nintendo.dk",
    "nintendo.de",
    "nintendo.fi",
    "nintendo.fr",
    "nintendo.gr",
    "nintendo.hu",
    "nintendo.it",
    "nintendo.nl",
    "nintendo.no",
    "nintendo.pt",
    "nintendo.ru",
    "nintendo.ch",
    "nintendo.se",
    "nintendoswitch.com.cn",
    "nintendoswitch.com",
    "sun.hac.lp1.d4c.nintendo.net",
    "phoenix-api.wbagora.com", //hogwarts legacy
    "battle.net",
    "microsoft.com", // Minecraft dungeons + other games
    "mojang.com",
    "xboxlive.com",
    "api.epicgames.dev", // marvel cosmic invasion +?
    "minecraftservices.com",
    "508223012e5a5ff19f30a391b2bdadc0.my.2k.com", // Civilization 5
};

static bool IsBlockedHost(const std::string& host) {
    return std::any_of(
        blockedDomains.begin(), blockedDomains.end(),
        [&host](const std::string& domain) { return host.find(domain) != std::string::npos; });
}

static NetDbError GetAddrInfoErrorToNetDbError(GetAddrInfoError result) {
    // These combinations have been verified on console (but are not
    // exhaustive).
    switch (result) {
    case GetAddrInfoError::SUCCESS:
        return NetDbError::Success;
    case GetAddrInfoError::AGAIN:
        return NetDbError::TryAgain;
    case GetAddrInfoError::NODATA:
        return NetDbError::HostNotFound;
    case GetAddrInfoError::SERVICE:
        return NetDbError::Success;
    default:
        return NetDbError::HostNotFound;
    }
}

static Errno GetAddrInfoErrorToErrno(GetAddrInfoError result) {
    // These combinations have been verified on console (but are not
    // exhaustive).
    switch (result) {
    case GetAddrInfoError::SUCCESS:
        // Note: Sometimes a successful lookup sets errno to EADDRNOTAVAIL for
        // some reason, but that doesn't seem useful to implement.
        return Errno::SUCCESS;
    case GetAddrInfoError::AGAIN:
        return Errno::SUCCESS;
    case GetAddrInfoError::NODATA:
        return Errno::SUCCESS;
    case GetAddrInfoError::SERVICE:
        return Errno::INVAL;
    default:
        return Errno::SUCCESS;
    }
}

template <typename T>
static void Append(std::vector<u8>& vec, T t) {
    const size_t offset = vec.size();
    vec.resize(offset + sizeof(T));
    std::memcpy(vec.data() + offset, &t, sizeof(T));
}

static void AppendNulTerminated(std::vector<u8>& vec, std::string_view str) {
    const size_t offset = vec.size();
    vec.resize(offset + str.size() + 1);
    std::memmove(vec.data() + offset, str.data(), str.size());
}

// We implement gethostbyname using the host's getaddrinfo rather than the
// host's gethostbyname, because it simplifies portability: e.g., getaddrinfo
// behaves the same on Unix and Windows, unlike gethostbyname where Windows
// doesn't implement h_errno.
static std::vector<u8> SerializeAddrInfoAsHostEnt(const std::vector<Network::AddrInfo>& vec,
                                                  std::string_view host) {

    std::vector<u8> data;
    // h_name: use the input hostname (append nul-terminated)
    AppendNulTerminated(data, host);
    // h_aliases: leave empty

    Append<u32_be>(data, 0); // count of h_aliases
    // (If the count were nonzero, the aliases would be appended as nul-terminated here.)
    Append<u16_be>(data, static_cast<u16>(Domain::INET)); // h_addrtype
    Append<u16_be>(data, sizeof(Network::IPv4Address));   // h_length
    // h_addr_list:
    size_t count = vec.size();
    ASSERT(count <= UINT32_MAX);
    Append<u32_be>(data, static_cast<uint32_t>(count));
    for (const Network::AddrInfo& addrinfo : vec) {
        // On the Switch, this is passed through htonl despite already being
        // big-endian, so it ends up as little-endian.
        Append<u32_le>(data, Network::IPv4AddressToInteger(addrinfo.addr.ip));

        LOG_INFO(Service, "Resolved host '{}' to IPv4 address {}", host,
                 Network::IPv4AddressToString(addrinfo.addr.ip));
    }
    return data;
}

static std::pair<u32, GetAddrInfoError> GetHostByNameRequestImpl(HLERequestContext& ctx) {
    struct InputParameters {
        u8 use_nsd_resolve;
        u32 cancel_handle;
        u64 process_id;
    };
    static_assert(sizeof(InputParameters) == 0x10);

    IPC::RequestParser rp{ctx};
    const auto parameters = rp.PopRaw<InputParameters>();

    LOG_WARNING(
        Service,
        "called with ignored parameters: use_nsd_resolve={}, cancel_handle={}, process_id={}",
        parameters.use_nsd_resolve, parameters.cancel_handle, parameters.process_id);

    const auto host = RequestedHost(ctx, parameters.use_nsd_resolve != 0);
    // For now, ignore options, which are in input buffer 1 for GetHostByNameRequestWithOptions.

    const auto redirect = PrivateServerRedirect(host);
    if (redirect) {
        ArmPrivateServerTrace();
    } else if (IsBlockedHost(host)) {
        LOG_WARNING(Network, "Resolution of hostname {} requested, returning EAI_AGAIN", host);
        return {0, GetAddrInfoError::AGAIN};
    }

    auto res_v = Network::GetAddressInfo(redirect.value_or(host), /*service*/ std::nullopt);
    if (auto* res = std::get_if<std::vector<Network::AddrInfo>>(&res_v)) {
        if (redirect) {
            for (auto& entry : *res) {
                entry.canon_name = host;
                RememberRedirectedHost(Network::IPv4AddressToString(entry.addr.ip), host);
            }
        }
        const std::vector<u8> data = SerializeAddrInfoAsHostEnt(*res, host);
        const u32 data_size = u32(data.size());
        ctx.WriteBuffer(data, 0);
        return {data_size, GetAddrInfoError::SUCCESS};
    }
    auto* err = std::get_if<Network::GetAddrInfoError>(&res_v);
    return {0, Translate(*err)};
}

void SFDNSRES::GetHostByNameRequest(HLERequestContext& ctx) {
    if (DeferNplnStartup(ctx)) {
        return;
    }
    auto [data_size, emu_gai_err] = GetHostByNameRequestImpl(ctx);

    struct OutputParameters {
        NetDbError netdb_error;
        Errno bsd_errno;
        u32 data_size;
    };
    static_assert(sizeof(OutputParameters) == 0xc);

    IPC::ResponseBuilder rb{ctx, 5};
    rb.Push(ResultSuccess);
    rb.PushRaw(OutputParameters{
        .netdb_error = GetAddrInfoErrorToNetDbError(emu_gai_err),
        .bsd_errno = GetAddrInfoErrorToErrno(emu_gai_err),
        .data_size = data_size,
    });
}

void SFDNSRES::GetHostByNameRequestWithOptions(HLERequestContext& ctx) {
    if (DeferNplnStartup(ctx)) {
        return;
    }
    auto [data_size, emu_gai_err] = GetHostByNameRequestImpl(ctx);

    struct OutputParameters {
        u32 data_size;
        NetDbError netdb_error;
        Errno bsd_errno;
    };
    static_assert(sizeof(OutputParameters) == 0xc);

    IPC::ResponseBuilder rb{ctx, 5};
    rb.Push(ResultSuccess);
    rb.PushRaw(OutputParameters{
        .data_size = data_size,
        .netdb_error = GetAddrInfoErrorToNetDbError(emu_gai_err),
        .bsd_errno = GetAddrInfoErrorToErrno(emu_gai_err),
    });
}

static std::vector<u8> SerializeAddrInfo(const std::vector<Network::AddrInfo>& vec,
                                         std::string_view host, u32 flags = 0,
                                         bool ryujinx_ipv4 = false) {
    // Adapted from
    // https://github.com/switchbrew/libnx/blob/c5a9a909a91657a9818a3b7e18c9b91ff0cbb6e3/nx/source/runtime/resolver.c#L190
    std::vector<u8> data;

    for (const Network::AddrInfo& addrinfo : vec) {
        // serialized addrinfo:
        const u32 wire_type = ryujinx_ipv4 ? 0 : static_cast<u32>(Translate(addrinfo.socket_type));
        const u32 wire_protocol = ryujinx_ipv4 ? 0 : static_cast<u32>(Translate(addrinfo.protocol));
        Append<u32_be>(data, 0xBEEFCAFE);                                   // magic
        Append<u32_be>(data, ryujinx_ipv4 ? 0 : flags);                      // ai_flags
        Append<u32_be>(data, static_cast<u32>(Translate(addrinfo.family))); // ai_family
        Append<u32_be>(data, wire_type);                                   // ai_socktype
        Append<u32_be>(data, wire_protocol);                               // ai_protocol
        Append<u32_be>(data, sizeof(SockAddrIn));                           // ai_addrlen

        // ai_addr is a BSD sockaddr_in: the length and family are separate bytes.
        // Match GRID0-ryujinx's AddrInfo4 wire representation for Splatoon NPLN:
        // ai_addrlen remains 16, while the embedded sin_len byte is 4.
        Append<u8>(data, ryujinx_ipv4 ? 4 : 16);                              // sin_len
        Append<u8>(data, static_cast<u8>(Translate(addrinfo.addr.family))); // sin_family
        // On the Switch, the following fields are passed through htonl despite
        // already being big-endian, so they end up as little-endian.
        Append<u16_le>(data, addrinfo.addr.portno);                            // sin_port
        Append<u32_le>(data, Network::IPv4AddressToInteger(addrinfo.addr.ip)); // sin_addr
        data.resize(data.size() + 8, 0);                                       // sin_zero

        if (addrinfo.canon_name.has_value()) {
            AppendNulTerminated(data, *addrinfo.canon_name);
        } else {
            data.push_back(0);
        }

        LOG_INFO(Service, "Resolved host '{}' to IPv4 address {}", host,
                 Network::IPv4AddressToString(addrinfo.addr.ip));
    }

    data.resize(data.size() + 4, 0); // 4-byte sentinel value

    return data;
}

void SFDNSRES::SetDeferredPollWaker(std::shared_ptr<DeferredPollWaker> waker) {
    deferred_poll_waker = std::move(waker);
}

bool SFDNSRES::DeferNplnStartup(HLERequestContext& ctx) {
    if (!deferred_poll_waker || PrivateServerAddress().empty()) {
        return false;
    }
    const auto host = ResolveNsdFqdn(Common::StringFromBuffer(ctx.ReadBuffer(0)));
    std::scoped_lock lock{npln_startup_mutex};
    const auto deadline = npln_startup.WaitUntil(
        system.GetApplicationProcessProgramID(), PrivateServerRedirect(host).has_value(), host,
        std::chrono::steady_clock::now());
    if (!deadline) {
        return false;
    }
    // Guest pointer buffers can be reused while parked; retain at most three 4 KiB inputs.
    if (!ctx.SnapshotReadBuffers(4096)) {
        LOG_WARNING(Network, "GRID0+ NPLN startup wait skipped: oversized resolver input");
        return false;
    }
    ctx.SetDeferredWakeup([waker = deferred_poll_waker, deadline = *deadline] {
        waker->WakeAt(deadline);
    });
    ctx.SetIsDeferred();
    if (!npln_startup_logged) {
        npln_startup_logged = true;
        LOG_INFO(Network, "GRID0+ NPLN startup: delaying first lookup of {} by 2500 ms", host);
    }
    return true;
}

static std::pair<u32, GetAddrInfoError> GetAddrInfoRequestImpl(HLERequestContext& ctx, u64 program_id) {
    struct InputParameters {
        u8 use_nsd_resolve;
        u32 cancel_handle;
        u64 process_id;
    };
    static_assert(sizeof(InputParameters) == 0x10);

    IPC::RequestParser rp{ctx};
    const auto parameters = rp.PopRaw<InputParameters>();

    LOG_WARNING(
        Service,
        "called with ignored parameters: use_nsd_resolve={}, cancel_handle={}, process_id={}",
        parameters.use_nsd_resolve, parameters.cancel_handle, parameters.process_id);

    const auto host = RequestedHost(ctx, parameters.use_nsd_resolve != 0);

    const auto redirect = PrivateServerRedirect(host);
    if (redirect) {
        ArmPrivateServerTrace();
    } else if (IsBlockedHost(host)) {
        LOG_WARNING(Network, "Resolution of hostname {} requested, returning EAI_AGAIN", host);
        return {0, GetAddrInfoError::AGAIN};
    }

    std::optional<std::string> service = std::nullopt;
    if (ctx.CanReadBuffer(1)) {
        const std::span<const u8> service_buffer = ctx.ReadBuffer(1);
        service = Common::StringFromBuffer(service_buffer);
    }

    Network::Type socket_type = Network::Type::Unspecified;
    Network::Protocol protocol = Network::Protocol::Unspecified;
    u32 hint_flags = 0;
    u32 hint_family = 0;
    u32 hint_type = 0;
    u32 hint_protocol = 0;
    if (ctx.CanReadBuffer(2) && ctx.GetReadBufferSize(2) != 0) {
        const auto hints = ctx.ReadBuffer(2);
        if (hints.size() < sizeof(u32)) {
            LOG_ERROR(Network, "Resolver: truncated hints for {} ({} bytes)", host, hints.size());
            return {0, GetAddrInfoError::BADHINTS};
        }
        const auto read_be32 = [&hints](size_t offset) {
            u32_be value;
            std::memcpy(&value, hints.data() + offset, sizeof(value));
            return static_cast<u32>(value);
        };
        const u32 magic = read_be32(0);
        if (magic != 0 && magic != 0xBEEFCAFE) {
            LOG_ERROR(Network, "Resolver: invalid hints magic {:#x} for {}", magic, host);
            return {0, GetAddrInfoError::BADHINTS};
        }
        if (magic != 0) {
            if (hints.size() < 24) {
                LOG_ERROR(Network, "Resolver: truncated addrinfo header for {} ({} bytes)",
                          host, hints.size());
                return {0, GetAddrInfoError::BADHINTS};
            }
            hint_flags = read_be32(4);
            hint_family = read_be32(8);
            hint_type = read_be32(12);
            hint_protocol = read_be32(16);
            if (hint_family != 0 && hint_family != static_cast<u32>(Domain::INET)) {
                LOG_WARNING(Network, "Resolver: unsupported requested family {} for {}", hint_family, host);
                return {0, GetAddrInfoError::FAMILY};
            }
            switch (static_cast<Type>(hint_type)) {
            case Type::Unspecified: break;
            case Type::STREAM: socket_type = Network::Type::STREAM; break;
            case Type::DGRAM: socket_type = Network::Type::DGRAM; break;
            case Type::RAW: socket_type = Network::Type::RAW; break;
            case Type::SEQPACKET: socket_type = Network::Type::SEQPACKET; break;
            default: return {0, GetAddrInfoError::SOCKTYPE};
            }
            switch (static_cast<Protocol>(hint_protocol)) {
            case Protocol::IP: break;
            case Protocol::TCP: protocol = Network::Protocol::TCP; break;
            case Protocol::UDP: protocol = Network::Protocol::UDP; break;
            case Protocol::ICMP: protocol = Network::Protocol::ICMP; break;
            case Protocol::RAW: protocol = Network::Protocol::RAW; break;
            default: return {0, GetAddrInfoError::PROTOCOL};
            }
        }
    }
    auto res_v = Network::GetAddressInfo(redirect.value_or(host), service, socket_type, protocol);
    if (auto* res = std::get_if<std::vector<Network::AddrInfo>>(&res_v)) {
        if (redirect) {
            for (auto& entry : *res) {
                entry.canon_name = host;
                RememberRedirectedHost(Network::IPv4AddressToString(entry.addr.ip), host);
            }
        }
        const std::vector<u8> data = SerializeAddrInfo(*res, host, hint_flags,
            program_id == 0x0100C2500FC20000ULL &&
            redirect.has_value() && host.find(".npln.") != std::string::npos);
        const u32 data_size = u32(data.size());
        ctx.WriteBuffer(data, 0);
        return {data_size, GetAddrInfoError::SUCCESS};
    }
    auto* err = std::get_if<Network::GetAddrInfoError>(&res_v);
    return {0, Translate(*err)};
}

void SFDNSRES::GetAddrInfoRequest(HLERequestContext& ctx) {
    if (DeferNplnStartup(ctx)) {
        return;
    }
    auto [data_size, emu_gai_err] = GetAddrInfoRequestImpl(ctx, system.GetApplicationProcessProgramID());

    struct OutputParameters {
        Errno bsd_errno;
        GetAddrInfoError gai_error;
        u32 data_size;
    };
    static_assert(sizeof(OutputParameters) == 0xc);

    IPC::ResponseBuilder rb{ctx, 5};
    rb.Push(ResultSuccess);
    rb.PushRaw(OutputParameters{
        .bsd_errno = GetAddrInfoErrorToErrno(emu_gai_err),
        .gai_error = emu_gai_err,
        .data_size = data_size,
    });
}

void SFDNSRES::GetGaiStringErrorRequest(HLERequestContext& ctx) {
    struct InputParameters {
        GetAddrInfoError gai_errno;
    };
    IPC::RequestParser rp{ctx};
    auto input = rp.PopRaw<InputParameters>();

    const std::string result = Translate(input.gai_errno);
    ctx.WriteBuffer(result);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void SFDNSRES::GetAddrInfoRequestWithOptions(HLERequestContext& ctx) {
    if (DeferNplnStartup(ctx)) {
        return;
    }
    // Additional options are ignored
    auto [data_size, emu_gai_err] = GetAddrInfoRequestImpl(ctx, system.GetApplicationProcessProgramID());

    struct OutputParameters {
        u32 data_size;
        GetAddrInfoError gai_error;
        NetDbError netdb_error;
        Errno bsd_errno;
    };
    static_assert(sizeof(OutputParameters) == 0x10);

    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(ResultSuccess);
    rb.PushRaw(OutputParameters{
        .data_size = data_size,
        .gai_error = emu_gai_err,
        .netdb_error = GetAddrInfoErrorToNetDbError(emu_gai_err),
        .bsd_errno = GetAddrInfoErrorToErrno(emu_gai_err),
    });
}

void SFDNSRES::ResolverSetOptionRequest(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 3};

    rb.Push(ResultSuccess);
    rb.Push<s32>(0); // bsd errno
}


DNS_PRIV::DNS_PRIV(Core::System& system_)
    : ServiceFramework{system_, "dns:priv"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "Cmd0"},
        {1, nullptr, "Cmd1"},
        {2, nullptr, "Cmd2"},
    };
    // clang-format on
    RegisterHandlers(functions);
}

DNS_PRIV::~DNS_PRIV() = default;


} // namespace Service::Sockets
