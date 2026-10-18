// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstring>
#include <limits>
#include <catch2/catch_test_macros.hpp>
#include "core/hle/service/sockets/message_batch.h"

using namespace Service::Sockets;

namespace {
void Append(std::vector<u8>& buffer, u64 value, size_t size) {
    const size_t offset = buffer.size();
    buffer.resize(offset + size);
    std::memcpy(buffer.data() + offset, &value, size);
}

void Message(std::vector<u8>& buffer, std::initializer_list<std::string_view> iovs) {
    Append(buffer, 0, 4); // no destination name
    Append(buffer, iovs.size(), 4);
    for (const auto data : iovs) {
        Append(buffer, data.size(), 8);
        buffer.insert(buffer.end(), data.begin(), data.end());
    }
    Append(buffer, 0, 4); // no control data
    Append(buffer, 0, 4); // flags
    Append(buffer, 0, 4); // transferred length
}

u32 Length(const std::vector<u8>& buffer, const SocketMessage& message) {
    u32 value;
    std::memcpy(&value, buffer.data() + message.length_offset, sizeof(value));
    return value;
}
} // namespace

TEST_CASE("Message batches reject truncated and overflowing guest lengths before sending", "[grid0][mmsg]") {
    std::vector<u8> buffer{8};
    Message(buffer, {"TLS", "record"});
    for (size_t size = 0; size < buffer.size(); ++size) {
        unsigned calls = 0;
        auto truncated = buffer;
        truncated.resize(size);
        const auto result = TransferMessageBatch(truncated, 1, 0, false, true,
            [&](auto&, auto&, u32) { ++calls; return std::pair{0, Errno::SUCCESS}; });
        REQUIRE(result.first == -1);
        REQUIRE(calls == 0);
    }
    const auto original = buffer;
    const u64 huge = std::numeric_limits<u64>::max();
    std::memcpy(buffer.data() + 9, &huge, sizeof(huge));
    REQUIRE_FALSE(ParseMessageBatch(buffer, 1));
    REQUIRE_FALSE(ParseMessageBatch(original, 0));
    REQUIRE_FALSE(ParseMessageBatch(original, 1025));
    REQUIRE_FALSE(ParseMessageBatch(original, 2));
}

TEST_CASE("SendMMsg gathers vectors and reports short sends and partial batch success", "[grid0][mmsg]") {
    std::vector<u8> buffer{0};
    Message(buffer, {"TLS", "record"});
    Message(buffer, {"second"});
    const auto layout = ParseMessageBatch(buffer, 2);
    REQUIRE(layout);
    unsigned calls = 0;
    SECTION("short stream send") {
        const auto result = TransferMessageBatch(buffer, 2, 0, false, true,
            [&](auto& data, auto& address, u32) {
                ++calls;
                REQUIRE(std::string(data.begin(), data.end()) == "TLSrecord");
                REQUIRE(address.empty());
                return std::pair{4, Errno::SUCCESS};
            });
        REQUIRE(result == std::pair{1, Errno::SUCCESS});
        REQUIRE(calls == 1);
        REQUIRE(Length(buffer, layout->at(0)) == 4);
        REQUIRE(Length(buffer, layout->at(1)) == 0);
    }
    SECTION("later send would block") {
        const auto result = TransferMessageBatch(buffer, 2, 0, false, true,
            [&](auto& data, auto&, u32) {
                return ++calls == 1 ? std::pair{static_cast<s32>(data.size()), Errno::SUCCESS}
                                    : std::pair{-1, Errno::AGAIN};
            });
        REQUIRE(result == std::pair{1, Errno::SUCCESS});
        REQUIRE(Length(buffer, layout->at(0)) == 9);
        REQUIRE(calls == 2);
    }
    SECTION("first send fails") {
        const auto result = TransferMessageBatch(buffer, 2, 0, false, true,
            [&](auto&, auto&, u32) { return std::pair{-1, Errno::CONNRESET}; });
        REQUIRE(result == std::pair{-1, Errno::CONNRESET});
    }
    REQUIRE(buffer.front() == 8);
}

TEST_CASE("RecvMMsg preserves datagram boundaries and scatters across vectors", "[grid0][mmsg]") {
    std::vector<u8> buffer{8};
    Message(buffer, {"....", "...."});
    Message(buffer, {"........"});
    const auto layout = ParseMessageBatch(buffer, 2);
    REQUIRE(layout);
    unsigned calls = 0;
    const auto result = TransferMessageBatch(buffer, 2, 0, true, false,
        [&](auto& data, auto&, u32 flags) {
            REQUIRE(flags == (calls ? Network::FLAG_MSG_DONTWAIT : 0));
            const std::string_view packet = calls++ ? "reply" : "hello!";
            std::copy(packet.begin(), packet.end(), data.begin());
            return std::pair{static_cast<s32>(packet.size()), Errno::SUCCESS};
        });
    REQUIRE(result == std::pair{2, Errno::SUCCESS});
    REQUIRE(Length(buffer, layout->at(0)) == 6);
    REQUIRE(Length(buffer, layout->at(1)) == 5);
    const auto first = GatherSocketMessage(buffer, layout->at(0));
    REQUIRE(std::string(first.begin(), first.begin() + 6) == "hello!");
    const auto second = GatherSocketMessage(buffer, layout->at(1));
    REQUIRE(std::string(second.begin(), second.begin() + 5) == "reply");
}
