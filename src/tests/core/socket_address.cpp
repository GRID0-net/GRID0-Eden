// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "core/hle/service/sockets/socket_address.h"

using namespace Service::Sockets;

TEST_CASE("Guest IPv4 addresses decode from wire and storage buffers", "[grid0][sockets]") {
    const std::array<u8, 16> wire{16, 2, 0x01, 0xbb, 192, 0, 2, 1};
    for (const auto size : {16, 28, 128, 256}) {
        std::vector<u8> buffer(static_cast<std::size_t>(size), 0xaa);
        std::copy(wire.begin(), wire.end(), buffer.begin());
        SockAddrIn address{};
        REQUIRE(ReadSockAddrIn(buffer, address) == Errno::SUCCESS);
        REQUIRE(address.len == 16);
        REQUIRE(address.family == 2);
        REQUIRE(address.ip == std::array<u8, 4>{192, 0, 2, 1});
        const auto* port = reinterpret_cast<const u8*>(&address.portno);
        REQUIRE(port[0] == 0x01);
        REQUIRE(port[1] == 0xbb);
        REQUIRE(address.zeroes == std::array<u8, 8>{});
    }
}

TEST_CASE("Guest IPv6 and truncated addresses are rejected before translation", "[grid0][sockets]") {
    SockAddrIn address{};
    std::array<u8, 28> ipv6{28, 28};
    REQUIRE(ReadSockAddrIn(ipv6, address) == Errno::AFNOSUPPORT);
    const std::array<u8, 16> ipv4{16, 2};
    for (std::size_t length = 0; length < ipv4.size(); ++length) {
        REQUIRE(ReadSockAddrIn(std::span(ipv4).first(length), address) == Errno::INVAL);
    }
    std::array<u8, 256> storage{};
    storage[1] = 28;
    REQUIRE(ReadSockAddrIn(storage, address) == Errno::AFNOSUPPORT);
}
