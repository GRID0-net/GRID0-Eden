// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstring>
#include <catch2/catch_test_macros.hpp>
#include "core/hle/service/sockets/eventfd.h"

using namespace Service::Sockets;

TEST_CASE("EventFd parameters put flags before the aligned initial counter", "[grid0][eventfd]") {
    const std::array<u8, 16> wire{6, 0, 0, 0, 0, 0, 0, 0, 42};
    EventFdParameters input;
    std::memcpy(&input, wire.data(), wire.size());
    REQUIRE(input.flags == 6);
    REQUIRE(input.initval == 42);
}

TEST_CASE("Polling an eventfd leaves the wakeup available for its reader", "[grid0][eventfd]") {
    EventFdState event{0, false};
    REQUIRE_FALSE(event.Readable());
    REQUIRE(event.Write(3) == Errno::SUCCESS);
    REQUIRE(event.Readable());
    REQUIRE(event.Readable());
    REQUIRE(event.Read() == std::pair{u64{3}, Errno::SUCCESS});
    REQUIRE_FALSE(event.Readable());
    REQUIRE(event.Read().second == Errno::AGAIN);
    REQUIRE(event.Write(~u64{0}) == Errno::INVAL);
    REQUIRE(event.Write(~u64{0} - 1) == Errno::SUCCESS);
    REQUIRE(event.Write(1) == Errno::AGAIN);
}

TEST_CASE("Semaphore eventfds consume one wakeup per read", "[grid0][eventfd]") {
    EventFdState event{2, true};
    REQUIRE(event.Read() == std::pair{u64{1}, Errno::SUCCESS});
    REQUIRE(event.Readable());
    REQUIRE(event.Read() == std::pair{u64{1}, Errno::SUCCESS});
    REQUIRE_FALSE(event.Readable());
}
