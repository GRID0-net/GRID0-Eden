// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstring>
#include <span>
#include "core/hle/service/sockets/sockets.h"

namespace Service::Sockets {

// A larger IPC buffer may hold sockaddr_storage; only the IPv4 prefix is an address.
inline Errno ReadSockAddrIn(std::span<const u8> buffer, SockAddrIn& address) {
    if (buffer.size() < 2) {
        return Errno::INVAL;
    }
    if (buffer[1] != static_cast<u8>(Domain::INET)) {
        return Errno::AFNOSUPPORT;
    }
    if (buffer.size() < sizeof(SockAddrIn)) {
        return Errno::INVAL;
    }
    std::memcpy(&address, buffer.data(), sizeof(address));
    return Errno::SUCCESS;
}

} // namespace Service::Sockets
