// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstring>
#include <string_view>
#include <catch2/catch_test_macros.hpp>
#include "core/hle/service/sockets/message_batch.h"
#include "core/hle/service/sockets/sockets_translate.h"
#include "core/internal_network/network.h"
#include "core/internal_network/sockets.h"

namespace {
void Word(std::vector<u8>& buffer, u64 value, size_t size) {
    const auto offset = buffer.size();
    buffer.resize(offset + size);
    std::memcpy(buffer.data() + offset, &value, size);
}

std::vector<u8> Batch() {
    std::vector<u8> buffer{8};
    for (const auto packet : {std::string_view{"first"}, std::string_view{"second"}}) {
        Word(buffer, 0, 4);
        Word(buffer, 1, 4);
        Word(buffer, packet.size(), 8);
        buffer.insert(buffer.end(), packet.begin(), packet.end());
        Word(buffer, 0, 4);
        Word(buffer, 0, 4);
        Word(buffer, 0, 4);
    }
    return buffer;
}
} // namespace

TEST_CASE("Message batches transfer distinct UDP packets over real sockets", "[grid0][mmsg][network]") {
    using namespace Service::Sockets;
    Network::NetworkInstance network;
    Network::Socket receiver;
    Network::Socket sender;
    REQUIRE(receiver.Initialize(Network::Domain::INET, Network::Type::DGRAM,
                                 Network::Protocol::UDP) == Network::Errno::SUCCESS);
    REQUIRE(sender.Initialize(Network::Domain::INET, Network::Type::DGRAM,
                               Network::Protocol::UDP) == Network::Errno::SUCCESS);
    REQUIRE(receiver.Bind({Network::Domain::INET, {127, 0, 0, 1}, 0}) == Network::Errno::SUCCESS);
    const auto [destination, address_error] = receiver.GetSockName();
    REQUIRE(address_error == Network::Errno::SUCCESS);
    REQUIRE(receiver.SetRcvTimeo(2000) == Network::Errno::SUCCESS);
    auto outgoing = Batch();
    const auto sent = TransferMessageBatch(outgoing, 2, 0, false, false,
        [&](auto& data, auto&, u32 flags) {
            return Translate(sender.SendTo(flags, data, &destination));
        });
    REQUIRE(sent == std::pair{2, Errno::SUCCESS});
    std::array<u8, 16> peeked{};
    const auto [peek_size, peek_error] = receiver.Recv(Network::FLAG_MSG_PEEK, peeked);
    REQUIRE(peek_error == Network::Errno::SUCCESS);
    REQUIRE(peek_size == 5);
    REQUIRE(std::string(peeked.begin(), peeked.begin() + peek_size) == "first");
    auto incoming = Batch();
    const auto received = TransferMessageBatch(incoming, 2, 0, true, false,
        [&](auto& data, auto&, u32 flags) {
            REQUIRE(receiver.SetNonBlock((flags & Network::FLAG_MSG_DONTWAIT) != 0) == Network::Errno::SUCCESS);
            return Translate(receiver.Recv(0, data));
        });
    REQUIRE(received == std::pair{2, Errno::SUCCESS});
    const auto messages = ParseMessageBatch(incoming, 2);
    REQUIRE(messages);
    const auto first = GatherSocketMessage(incoming, messages->at(0));
    const auto second = GatherSocketMessage(incoming, messages->at(1));
    REQUIRE(std::string(first.begin(), first.end()) == "first");
    REQUIRE(std::string(second.begin(), second.end()) == "second");
}

TEST_CASE("A zero-protocol TCP socket and filtered DNS hints work", "[grid0][network]") {
    Network::NetworkInstance network;
    Network::Socket socket;
    REQUIRE(socket.Initialize(Network::Domain::INET, Network::Type::STREAM,
                               Network::Protocol::IP) == Network::Errno::SUCCESS);
    const auto result = Network::GetAddressInfo("127.0.0.1", "443", Network::Type::STREAM,
                                               Network::Protocol::TCP);
    const auto* addresses = std::get_if<std::vector<Network::AddrInfo>>(&result);
    REQUIRE(addresses);
    REQUIRE_FALSE(addresses->empty());
    for (const auto& address : *addresses) {
        REQUIRE(address.socket_type == Network::Type::STREAM);
        REQUIRE(address.protocol == Network::Protocol::TCP);
        REQUIRE(address.addr.portno == 443);
    }
}
