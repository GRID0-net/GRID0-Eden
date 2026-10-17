// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include "common/scope_exit.h"
#include "common/settings.h"
#include "core/hle/service/sockets/private_server.h"

TEST_CASE("GRID0 DNS redirection respects domain boundaries and the second NAT server", "[grid0]") {
    const auto primary = Settings::values.private_server_address.GetValue();
    const auto secondary = Settings::values.private_server_nat_secondary_address.GetValue();
    const auto login = Settings::values.switchnet_server.GetValue();
    const auto username = Settings::values.switchnet_username.GetValue();
    SCOPE_EXIT {
        Settings::values.private_server_address = primary;
        Settings::values.private_server_nat_secondary_address = secondary;
        Settings::values.switchnet_server = login;
        Settings::values.switchnet_username = username;
    };
    Settings::values.private_server_address = "192.0.2.1";
    Settings::values.private_server_nat_secondary_address = "192.0.2.2";
    REQUIRE(Service::Sockets::PrivateServerRedirect("api.nintendo.net") == "192.0.2.1");
    REQUIRE(Service::Sockets::PrivateServerRedirect("API.NINTENDO.NET.") == "192.0.2.1");
    REQUIRE(Service::Sockets::PrivateServerRedirect("nncs2.nintendo.net") == "192.0.2.2");
    REQUIRE_FALSE(Service::Sockets::PrivateServerRedirect("example.org"));
    REQUIRE_FALSE(Service::Sockets::PrivateServerRedirect("fakenintendo.net"));
    REQUIRE_FALSE(Service::Sockets::PrivateServerRedirect("nintendo.net.example.org"));
    Settings::values.private_server_address = "";
    Settings::values.switchnet_username = "fixture";
    Settings::values.switchnet_server = "192.0.2.3:443";
    REQUIRE(Service::Sockets::PrivateServerRedirect("api.nintendo.net") == "192.0.2.3");
    Settings::values.switchnet_server = "";
    REQUIRE_FALSE(Service::Sockets::PrivateServerRedirect("api.nintendo.net"));
}
