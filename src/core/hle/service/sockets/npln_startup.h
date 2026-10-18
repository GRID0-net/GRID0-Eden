// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Service::Sockets {

class NplnStartupGate {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto Delay = std::chrono::milliseconds(2500);

    std::optional<Clock::time_point> WaitUntil(std::uint64_t program_id, bool redirected,
                                             std::string_view host, Clock::time_point now) {
        if (released || program_id != 0x0100C2500FC20000ULL || !redirected ||
            host.find(".npln.") == std::string_view::npos) {
            return std::nullopt;
        }
        if (!deadline) {
            deadline = now + Delay;
        }
        if (now >= *deadline) {
            released = true;
            return std::nullopt;
        }
        return deadline;
    }

private:
    std::optional<Clock::time_point> deadline;
    bool released = false;
};

} // namespace Service::Sockets
