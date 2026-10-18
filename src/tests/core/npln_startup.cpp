// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include "core/hle/service/sockets/npln_startup.h"

using namespace Service::Sockets;
using namespace std::chrono_literals;

TEST_CASE("NPLN startup waits once across concurrent resolver requests", "[grid0][npln]") {
    NplnStartupGate gate;
    const auto start = NplnStartupGate::Clock::time_point{} + 1s;
    constexpr auto id = 0x0100C2500FC20000ULL;
    constexpr auto host = "t-dce9377b-lp1.lp1.t.npln.srv.nintendo.net";
    REQUIRE(gate.WaitUntil(id, true, host, start) == start + 2500ms);
    REQUIRE(gate.WaitUntil(id, true, host, start + 1s) == start + 2500ms);
    REQUIRE(gate.WaitUntil(id, true, host, start + 2499ms) == start + 2500ms);
    REQUIRE_FALSE(gate.WaitUntil(id, true, host, start + 2500ms));
    REQUIRE_FALSE(gate.WaitUntil(id, true, host, start + 3s));
    REQUIRE_FALSE(gate.WaitUntil(id, true, host, start + 1h));
    NplnStartupGate next_game;
    REQUIRE(next_game.WaitUntil(id, true, host, start + 1h) == start + 1h + 2500ms);
}

TEST_CASE("The startup wait only applies to redirected Splatoon NPLN lookups", "[grid0][npln]") {
    NplnStartupGate gate;
    const auto start = NplnStartupGate::Clock::time_point{};
    constexpr auto id = 0x0100C2500FC20000ULL;
    constexpr auto host = "t-dce9377b-lp1.lp1.t.npln.srv.nintendo.net";
    REQUIRE_FALSE(gate.WaitUntil(id + 1, true, host, start));
    REQUIRE_FALSE(gate.WaitUntil(id, false, host, start));
    REQUIRE_FALSE(gate.WaitUntil(id, true, "accounts.nintendo.com", start));
    REQUIRE_FALSE(gate.WaitUntil(id, true, "", start));
    REQUIRE(gate.WaitUntil(id, true, host, start + 5s) == start + 7500ms);
}
