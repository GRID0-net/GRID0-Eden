// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/settings.h"
#include "core/core.h"
#include "core/hle/service/acc/switchnet_account.h"
#include "core/hle/service/bcat/backend/grid0_backend.h"
#include "core/hle/service/bcat/bcat_service.h"
#include "core/hle/service/bcat/delivery_cache_storage_service.h"
#include "core/hle/service/bcat/service_creator.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/filesystem.h"

namespace Service::BCAT {

std::unique_ptr<BcatBackend> CreateBackendFromSettings([[maybe_unused]] Core::System& system,
                                                       DirectoryGetter getter) {
    // Signed in to GRID0+, a game's delivery cache comes from the server, as on a console.
    if (Service::Account::SwitchNet::IsConfigured()) {
        return std::make_unique<Grid0BcatBackend>(std::move(getter));
    }
    return std::make_unique<NullBcatBackend>(std::move(getter));
}

IServiceCreator::IServiceCreator(Core::System& system_, const char* name_)
    : ServiceFramework{system_, name_}, fsc{system.GetFileSystemController()} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IServiceCreator::CreateBcatService>, "CreateBcatService"},
        {1, D<&IServiceCreator::CreateDeliveryCacheStorageService>, "CreateDeliveryCacheStorageService"},
        {2, D<&IServiceCreator::CreateDeliveryCacheStorageServiceWithApplicationId>, "CreateDeliveryCacheStorageServiceWithApplicationId"},
        {3, nullptr, "CreateDeliveryCacheProgressService"},
        {4, nullptr, "CreateDeliveryCacheProgressServiceWithApplicationId"},
    };
    // clang-format on

    RegisterHandlers(functions);

    backend =
        CreateBackendFromSettings(system_, [this](u64 tid) { return fsc.GetBCATDirectory(tid); });
}

IServiceCreator::~IServiceCreator() = default;

void IServiceCreator::PrefetchDeliveryCache(u64 title_id) {
    if (title_id == 0 || prefetched_title == title_id ||
        !Service::Account::SwitchNet::IsConfigured() || Settings::values.airplane_mode.GetValue()) {
        return;
    }
    // A console downloads BCAT independently of the game's explicit sync requests.
    // Match Ryujinx's initial prefetch before exposing a delivery-cache view. Reuse
    // this BCAT worker and the existing hash-checking backend; do not add a background thread.
    LOG_INFO(Service_BCAT, "GRID0+: prefetching delivery cache for {:016X}", title_id);
    ProgressServiceBackend progress{system, "GRID0+InitialSync"};
    if (backend->Prefetch(system.Kernel(), {title_id, 0}, progress)) {
        prefetched_title = title_id;
    }
}

Result IServiceCreator::CreateBcatService(ClientProcessId process_id,
                                          OutInterface<IBcatService> out_interface) {
    LOG_INFO(Service_BCAT, "called, process_id={}", process_id.pid);
    const auto title_id = system.ResolveCallerProgramId(*process_id);
    PrefetchDeliveryCache(title_id);
    *out_interface = std::make_shared<IBcatService>(system, *backend, title_id);
    R_SUCCEED();
}

Result IServiceCreator::CreateDeliveryCacheStorageService(
    ClientProcessId process_id, OutInterface<IDeliveryCacheStorageService> out_interface) {
    LOG_INFO(Service_BCAT, "called, process_id={}", process_id.pid);

    const auto title_id = system.ResolveCallerProgramId(*process_id);
    PrefetchDeliveryCache(title_id);
    *out_interface =
        std::make_shared<IDeliveryCacheStorageService>(system, fsc.GetBCATDirectory(title_id));
    R_SUCCEED();
}

Result IServiceCreator::CreateDeliveryCacheStorageServiceWithApplicationId(
    u64 application_id, OutInterface<IDeliveryCacheStorageService> out_interface) {
    LOG_DEBUG(Service_BCAT, "called, application_id={:016X}", application_id);
    PrefetchDeliveryCache(application_id);
    *out_interface = std::make_shared<IDeliveryCacheStorageService>(
        system, fsc.GetBCATDirectory(application_id));
    R_SUCCEED();
}

} // namespace Service::BCAT
