// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>

#include "common/thread.h"
#include "core/hle/service/sockets/deferred_poll_waker.h"

namespace Service::Sockets {

DeferredPollWaker::DeferredPollWaker(std::function<void()> signal_)
    : signal{std::move(signal_)}, thread{[this](std::stop_token stop) { Run(stop); }} {}

DeferredPollWaker::~DeferredPollWaker() {
    thread.request_stop();
    cv.notify_all();
}

void DeferredPollWaker::AddWaiter() {
    {
        std::scoped_lock lock{mutex};
        ++waiters;
    }
    cv.notify_all();
}

void DeferredPollWaker::RemoveWaiter() {
    std::scoped_lock lock{mutex};
    if (waiters > 0) {
        --waiters;
    }
}

void DeferredPollWaker::Wake() {
    signal();
}

void DeferredPollWaker::WakeAt(std::chrono::steady_clock::time_point deadline) {
    {
        std::scoped_lock lock{mutex};
        if (!wake_deadline || deadline < *wake_deadline) {
            wake_deadline = deadline;
        }
    }
    cv.notify_all();
}

void DeferredPollWaker::Run(std::stop_token stop) {
    Common::SetCurrentThreadName("bsd:DeferredPoll");

    while (!stop.stop_requested()) {
        bool poll_waiting{};
        bool timer_due{};
        {
            std::unique_lock lock{mutex};
            cv.wait(lock, stop, [this] { return waiters > 0 || wake_deadline.has_value(); });
            if (stop.stop_requested()) {
                break;
            }
            if (waiters == 0 && wake_deadline) {
                const auto deadline = *wake_deadline;
                cv.wait_until(lock, stop, deadline, [this, deadline] {
                    return waiters > 0 || !wake_deadline || *wake_deadline != deadline;
                });
            }
            if (stop.stop_requested()) {
                break;
            }
            poll_waiting = waiters > 0;
            timer_due = wake_deadline && std::chrono::steady_clock::now() >= *wake_deadline;
            if (timer_due) {
                wake_deadline.reset();
            }
        }
        if (poll_waiting) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (poll_waiting || timer_due) {
            signal();
        }
    }
}

} // namespace Service::Sockets
