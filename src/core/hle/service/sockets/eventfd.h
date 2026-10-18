// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <mutex>
#include <utility>
#include "common/common_types.h"
#include "core/hle/service/sockets/sockets.h"

namespace Service::Sockets {

struct EventFdParameters {
    u32 flags;
    u32 padding;
    u64 initval;
};
static_assert(sizeof(EventFdParameters) == 16);
static_assert(offsetof(EventFdParameters, initval) == 8);

class EventFdState {
public:
    EventFdState(u64 initial, bool semaphore_) : value{initial}, semaphore{semaphore_} {}

    bool Readable() {
        std::scoped_lock lock{mutex};
        return value != 0;
    }

    Errno Write(u64 add) {
        std::scoped_lock lock{mutex};
        if (add == ~u64{0}) {
            return Errno::INVAL;
        }
        if (value > ~u64{0} - 1 - add) {
            return Errno::AGAIN;
        }
        value += add;
        return Errno::SUCCESS;
    }

    std::pair<u64, Errno> Read() {
        std::scoped_lock lock{mutex};
        if (value == 0) {
            return {0, Errno::AGAIN};
        }
        if (semaphore) {
            --value;
            return {1, Errno::SUCCESS};
        }
        return {std::exchange(value, 0), Errno::SUCCESS};
    }

private:
    std::mutex mutex;
    u64 value;
    bool semaphore;
};

} // namespace Service::Sockets
