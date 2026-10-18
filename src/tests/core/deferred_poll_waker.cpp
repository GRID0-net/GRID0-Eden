// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <catch2/catch_test_macros.hpp>
#include "core/hle/service/sockets/deferred_poll_waker.h"

using namespace std::chrono_literals;

TEST_CASE("Resolver deadlines wake without a polling waiter and can move earlier", "[grid0][npln]") {
    std::mutex mutex;
    std::condition_variable cv;
    unsigned int signals = 0;
    std::chrono::steady_clock::time_point signalled_at;
    Service::Sockets::DeferredPollWaker waker([&] {
        std::scoped_lock lock{mutex};
        ++signals;
        signalled_at = std::chrono::steady_clock::now();
        cv.notify_all();
    });
    const auto deadline = std::chrono::steady_clock::now() + 20ms;
    waker.WakeAt(deadline + 1h);
    waker.WakeAt(deadline);
    waker.WakeAt(deadline + 1h);
    std::unique_lock lock{mutex};
    REQUIRE(cv.wait_for(lock, 1s, [&] { return signals != 0; }));
    REQUIRE(signalled_at >= deadline);
    REQUIRE(signals == 1);
    REQUIRE_FALSE(cv.wait_for(lock, 30ms, [&] { return signals > 1; }));
}

TEST_CASE("An expired deadline wakes immediately and shutdown cancels a future timer", "[grid0][npln]") {
    std::mutex mutex;
    std::condition_variable cv;
    bool signalled = false;
    const auto started = std::chrono::steady_clock::now();
    {
        Service::Sockets::DeferredPollWaker waker([&] {
            std::scoped_lock lock{mutex};
            signalled = true;
            cv.notify_all();
        });
        waker.WakeAt(started - 1s);
        {
            std::unique_lock lock{mutex};
            REQUIRE(cv.wait_for(lock, 1s, [&] { return signalled; }));
        }
        waker.WakeAt(started + 1h);
    }
    REQUIRE(std::chrono::steady_clock::now() - started < 1s);
}
