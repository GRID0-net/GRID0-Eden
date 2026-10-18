// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <fstream>
#include <catch2/catch_test_macros.hpp>
#include "frontend_common/config_file.h"

#define SI_NO_CONVERSION 1
#include <SimpleIni.h>

namespace {
struct ConfigFixture {
    std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("grid0-config-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path path = directory / std::filesystem::path(u8"settings-élise.ini");

    ConfigFixture() { std::filesystem::create_directory(directory); }
    ~ConfigFixture() {
        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
    }
    void Write(const std::string& contents) const {
        std::ofstream file(path, std::ios::binary);
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        REQUIRE(file.good());
    }
    std::string Read(const std::filesystem::path& filename) const {
        std::ifstream file(filename, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), {}};
    }
};
} // namespace

TEST_CASE("Settings preserve UTF-8 bytes and the original before replacement", "[config]") {
    ConfigFixture fixture;
    const std::string original = "\xef\xbb\xbf[UI]\r\ngamedirs\\size=1\r\n"
                                 "gamedirs\\1\\path=C:/games/élise/スプラトゥーン\r\n";
    fixture.Write(original);
    ConfigFile config;
    const auto loaded = config.Load(fixture.path);
    REQUIRE(loaded == original);
    CSimpleIniA ini;
    ini.SetUnicode(true);
    REQUIRE(ini.LoadData(loaded.data(), loaded.size()) >= 0);
    REQUIRE(std::string(ini.GetValue("UI", "gamedirs\\size", "missing")) == "1");
    REQUIRE(std::string(ini.GetValue("UI", "gamedirs\\1\\path", "missing")) ==
            "C:/games/élise/スプラトゥーン");
    config.Save("[UI]\ngamedirs\\size=2\n");
    REQUIRE(fixture.Read(fixture.path) == "[UI]\ngamedirs\\size=2\n");
    auto backup = fixture.path;
    backup += ".pre-grid0.bak";
    REQUIRE(fixture.Read(backup) == original);
    config.Save("third version");
    REQUIRE(fixture.Read(backup) == original);
    ConfigFile reopened;
    REQUIRE(reopened.Load(fixture.path) == "third version");
    reopened.Save("fourth version");
    REQUIRE(fixture.Read(backup) == original);
}

TEST_CASE("Settings never replace invalid, unreadable, or externally edited files", "[config]") {
    ConfigFixture fixture;
    ConfigFile config;
    SECTION("invalid byte encoding") {
        const std::string invalid("[U\0I]", 6);
        fixture.Write(invalid);
        REQUIRE_THROWS_AS(config.Load(fixture.path), ConfigFileError);
        REQUIRE_THROWS_AS(config.Save("defaults"), ConfigFileError);
        REQUIRE(fixture.Read(fixture.path) == invalid);
    }
    SECTION("unreadable file path") {
        std::filesystem::create_directory(fixture.path);
        REQUIRE_THROWS_AS(config.Load(fixture.path), ConfigFileError);
        REQUIRE_THROWS_AS(config.Save("defaults"), ConfigFileError);
        REQUIRE(std::filesystem::is_directory(fixture.path));
    }
    SECTION("another emulator writes while running") {
        fixture.Write("original");
        config.Load(fixture.path);
        fixture.Write("changed by regular Eden");
        REQUIRE_THROWS_AS(config.Save("stale settings"), ConfigFileError);
        REQUIRE(fixture.Read(fixture.path) == "changed by regular Eden");
    }
    SECTION("another emulator creates a previously missing file") {
        REQUIRE(config.Load(fixture.path).empty());
        fixture.Write("created by regular Eden");
        REQUIRE_THROWS_AS(config.Save("defaults"), ConfigFileError);
        REQUIRE(fixture.Read(fixture.path) == "created by regular Eden");
    }
    SECTION("backup cannot be written") {
        fixture.Write("original");
        config.Load(fixture.path);
        auto backup = fixture.path;
        backup += ".pre-grid0.bak";
        std::filesystem::create_directory(backup);
        REQUIRE_THROWS(config.Save("defaults"));
        REQUIRE(fixture.Read(fixture.path) == "original");
    }
}

TEST_CASE("A fresh settings file is created without requiring an existing file", "[config]") {
    ConfigFixture fixture;
    ConfigFile config;
    REQUIRE(config.Load(fixture.path).empty());
    REQUIRE_FALSE(std::filesystem::exists(fixture.path));
    config.Save("[UI]\ngamedirs\\size=0\n");
    REQUIRE(fixture.Read(fixture.path) == "[UI]\ngamedirs\\size=0\n");
}
