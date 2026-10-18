// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cstring>
#include <functional>
#include <utility>
#include <optional>
#include <span>
#include <vector>
#include <cstddef>

#include "common/common_types.h"
#include "common/socket_types.h"
#include "core/hle/service/sockets/sockets.h"

namespace Service::Sockets {

struct MessageRegion {
    size_t offset;
    size_t size;
};

struct SocketMessage {
    MessageRegion name;
    std::vector<MessageRegion> iovs;
    MessageRegion control;
    size_t flags_offset;
    size_t length_offset;
    size_t capacity = 0;
};

// Horizon serializes mmsghdrs into an in/out buffer, starting with a version byte.
inline std::optional<std::vector<SocketMessage>> ParseMessageBatch(std::span<const u8> buffer,
                                                                  u32 count) {
    if (buffer.empty() || count == 0 || count > 1024) {
        return std::nullopt;
    }
    size_t offset = 1;
    auto take = [&](u64 size) -> std::optional<MessageRegion> {
        if (size > buffer.size() - offset) {
            return std::nullopt;
        }
        MessageRegion region{offset, static_cast<size_t>(size)};
        offset += region.size;
        return region;
    };
    auto word = [&]() -> std::optional<u32> {
        const auto region = take(sizeof(u32));
        if (!region) {
            return std::nullopt;
        }
        u32 value;
        std::memcpy(&value, buffer.data() + region->offset, sizeof(value));
        return value;
    };
    std::vector<SocketMessage> messages;
    for (u32 i = 0; i < count; ++i) {
        SocketMessage message{};
        const auto name_size = word();
        const auto name = name_size ? take(*name_size) : std::nullopt;
        const auto iov_count = word();
        if (!name || !iov_count || *iov_count > 1024) {
            return std::nullopt;
        }
        message.name = *name;
        for (u32 j = 0; j < *iov_count; ++j) {
            const auto length = take(sizeof(u64));
            if (!length) {
                return std::nullopt;
            }
            u64 size;
            std::memcpy(&size, buffer.data() + length->offset, sizeof(size));
            const auto region = take(size);
            if (!region) {
                return std::nullopt;
            }
            message.iovs.push_back(*region);
            message.capacity += region->size;
        }
        const auto control_size = word();
        const auto control = control_size ? take(*control_size) : std::nullopt;
        const auto flags = take(sizeof(u32));
        const auto length = take(sizeof(u32));
        if (!control || !flags || !length) {
            return std::nullopt;
        }
        message.control = *control;
        message.flags_offset = flags->offset;
        message.length_offset = length->offset;
        messages.push_back(std::move(message));
    }
    return messages;
}

inline std::vector<u8> GatherSocketMessage(std::span<const u8> buffer,
                                          const SocketMessage& message) {
    std::vector<u8> data;
    data.reserve(message.capacity);
    for (const auto region : message.iovs) {
        const auto piece = buffer.subspan(region.offset, region.size);
        data.insert(data.end(), piece.begin(), piece.end());
    }
    return data;
}

inline void ScatterSocketMessage(std::span<u8> buffer, const SocketMessage& message,
                                 std::span<const u8> data) {
    for (const auto region : message.iovs) {
        const size_t size = std::min(region.size, data.size());
        if (size != 0) {
            std::memcpy(buffer.data() + region.offset, data.data(), size);
        }
        data = data.subspan(size);
    }
}

using MessageTransfer = std::function<std::pair<s32, Errno>(
    std::vector<u8>& data, std::vector<u8>& address, u32 flags)>;

inline std::pair<s32, Errno> TransferMessageBatch(std::span<u8> buffer, u32 count, u32 flags,
                                                bool receive, bool stream,
                                                const MessageTransfer& transfer) {
    const auto messages = ParseMessageBatch(buffer, count);
    if (!messages) {
        return {-1, Errno::INVAL};
    }
    s32 completed = 0;
    Errno error = Errno::SUCCESS;
    for (const auto& message : *messages) {
        if (message.control.size != 0) {
            error = Errno::INVAL;
            break;
        }
        std::vector<u8> data = receive ? std::vector<u8>(message.capacity)
                                       : GatherSocketMessage(buffer, message);
        const auto name = buffer.subspan(message.name.offset, message.name.size);
        std::vector<u8> address(name.begin(), name.end());
        const u32 call_flags = flags | (receive && completed ? Network::FLAG_MSG_DONTWAIT : 0);
        const auto [result, call_error] = transfer(data, address, call_flags);
        error = call_error;
        if (result < 0 || error != Errno::SUCCESS) {
            if (error == Errno::SUCCESS) {
                error = Errno::INVAL;
            }
            break;
        }
        const u32 length = static_cast<u32>(result);
        std::memcpy(buffer.data() + message.length_offset, &length, sizeof(length));
        if (receive) {
            ScatterSocketMessage(buffer, message, std::span<const u8>{data}.first(
                                     std::min(data.size(), static_cast<size_t>(result))));
            std::fill(name.begin(), name.end(), 0);
            if (!address.empty()) {
                std::memcpy(name.data(), address.data(), std::min(name.size(), address.size()));
            }
            const u32 output_flags = 0;
            std::memcpy(buffer.data() + message.flags_offset, &output_flags, sizeof(output_flags));
        }
        ++completed;
        if (stream && (result == 0 || static_cast<size_t>(result) < data.size())) {
            break;
        }
    }
    buffer[0] = 8;
    return completed ? std::pair{completed, Errno::SUCCESS} : std::pair{-1, error};
}

} // namespace Service::Sockets
