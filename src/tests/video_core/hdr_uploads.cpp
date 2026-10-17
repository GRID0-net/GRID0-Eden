// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstring>
#include <span>
#include <catch2/catch_test_macros.hpp>
#include "video_core/citrosis_diag.h"

TEST_CASE("HDR uploads preserve finite half floats and remove infinities and NaNs", "[hdr]") {
    std::array<u16, 8> data{0, 0x8000, 0x3c00, 0x0001, 0x7c00, 0xfc00, 0x7e00, 0xffff};
    auto bytes = std::span{reinterpret_cast<u8*>(data.data()), sizeof(data)};
    REQUIRE(VideoCommon::CitrosisDiag::ScrubNonFinite(
                VideoCore::Surface::PixelFormat::R16G16B16A16_FLOAT, bytes) == 4);
    REQUIRE(data == std::array<u16, 8>{0, 0x8000, 0x3c00, 0x0001, 0, 0, 0, 0});
}

TEST_CASE("HDR uploads preserve finite single floats and remove non-finite values", "[hdr]") {
    std::array<u32, 6> data{0x3f800000, 0x80000000, 1, 0x7f800000, 0xff800000, 0x7fc00000};
    auto bytes = std::span{reinterpret_cast<u8*>(data.data()), sizeof(data)};
    REQUIRE(VideoCommon::CitrosisDiag::ScrubNonFinite(
                VideoCore::Surface::PixelFormat::R32_FLOAT, bytes) == 3);
    REQUIRE(data == std::array<u32, 6>{0x3f800000, 0x80000000, 1, 0, 0, 0});
}

TEST_CASE("Packed HDR sanitizing changes only the non-finite channels", "[hdr]") {
    std::array<u32, 1> data{0xF8000000u | 0x00000400u};
    auto bytes = std::span{reinterpret_cast<u8*>(data.data()), sizeof(data)};
    REQUIRE(VideoCommon::CitrosisDiag::ScrubNonFinite(
                VideoCore::Surface::PixelFormat::B10G11R11_FLOAT, bytes) == 1);
    REQUIRE(data[0] == 0x00000400u);
}
