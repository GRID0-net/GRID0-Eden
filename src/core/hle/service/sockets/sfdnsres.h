// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <mutex>
#include "core/hle/service/service.h"
#include "core/hle/service/sockets/npln_startup.h"

namespace Core {
class System;
}

namespace Service::Sockets {

class DeferredPollWaker;

class SFDNSRES final : public ServiceFramework<SFDNSRES> {
public:
    explicit SFDNSRES(Core::System& system_);
    ~SFDNSRES() override;
    void SetDeferredPollWaker(std::shared_ptr<DeferredPollWaker> waker);

private:
    bool DeferNplnStartup(HLERequestContext& ctx);
    std::shared_ptr<DeferredPollWaker> deferred_poll_waker;
    std::mutex npln_startup_mutex;
    NplnStartupGate npln_startup;
    bool npln_startup_logged = false;
    void GetHostByNameRequest(HLERequestContext& ctx);
    void GetGaiStringErrorRequest(HLERequestContext& ctx);
    void GetHostByNameRequestWithOptions(HLERequestContext& ctx);
    void GetAddrInfoRequest(HLERequestContext& ctx);
    void GetAddrInfoRequestWithOptions(HLERequestContext& ctx);
    void ResolverSetOptionRequest(HLERequestContext& ctx);
};

class DNS_PRIV final : public ServiceFramework<DNS_PRIV> {
public:
    explicit DNS_PRIV(Core::System& system_);
    ~DNS_PRIV() override;
};

} // namespace Service::Sockets
