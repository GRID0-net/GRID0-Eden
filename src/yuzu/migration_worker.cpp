// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/fs/symlink.h"
#include "migration_worker.h"

#include <filesystem>
#include <stdexcept>
#include <vector>
#include <boost/algorithm/string/case_conv.hpp>

#include "common/fs/path_util.h"

MigrationWorker::MigrationWorker(const Emulator selected_emu_, const bool clear_shader_cache_,
                                 const MigrationStrategy strategy_)
    : QObject(), selected_emu(selected_emu_), clear_shader_cache(clear_shader_cache_),
      strategy(strategy_) {}

void MigrationWorker::process() {
    namespace fs = std::filesystem;

    const auto legacy_user_dir = Common::FS::GetLegacyPath(selected_emu.e_user_dir);
    const auto legacy_config_dir = Common::FS::GetLegacyPath(selected_emu.e_config_dir);
    const auto legacy_cache_dir = Common::FS::GetLegacyPath(selected_emu.e_cache_dir);

    // TODO(crueter): Make these constexpr since they're defaulted
    fs::path eden_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::EdenDir);
    fs::path config_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::ConfigDir);
    fs::path cache_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir);
    fs::path shader_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::ShaderDir);

    eden_dir.make_preferred();
    config_dir.make_preferred();
    cache_dir.make_preferred();
    shader_dir.make_preferred();

    std::vector<std::pair<fs::path, fs::path>> directories{{legacy_user_dir, eden_dir}};
#ifndef WIN32
    if (fs::is_directory(legacy_config_dir)) {
        directories.emplace_back(legacy_config_dir, config_dir);
    }
    if (fs::is_directory(legacy_cache_dir)) {
        directories.emplace_back(legacy_cache_dir, cache_dir);
    }
#endif

    const auto overlaps = [](const fs::path& a, const fs::path& b) {
        auto first = fs::weakly_canonical(a).generic_string();
        auto second = fs::weakly_canonical(b).generic_string();
#ifdef WIN32
        boost::algorithm::to_lower(first);
        boost::algorithm::to_lower(second);
#endif
        if (!first.ends_with('/')) {
            first += '/';
        }
        if (!second.ends_with('/')) {
            second += '/';
        }
        return first.starts_with(second) || second.starts_with(first);
    };
    const auto preserve = [this](const fs::path& destination) {
        if (!fs::exists(fs::symlink_status(destination))) {
            return;
        }
        auto backup = destination;
        unsigned int suffix = 0;
        do {
            backup = destination;
            backup += ".pre-grid0-migration-" + std::to_string(++suffix);
        } while (fs::exists(fs::symlink_status(backup)));
        fs::rename(destination, backup);
        success_text.append(tr("\n\nPrevious data was preserved at:\n%1")
                                .arg(QString::fromStdString(Common::FS::PathToUTF8String(backup))));
    };

    try {
        // Check every source before modifying any destination, including shared directories.
        for (const auto& [source, destination] : directories) {
            for (const auto& [other_source, other_destination] : directories) {
                if (overlaps(source, other_destination)) {
                    throw std::runtime_error("The old and new data directories overlap. "
                                             "No data was migrated.");
                }
            }
        }
        for (const auto& [source, destination] : directories) {
            preserve(destination);
            fs::create_directories(destination.parent_path());
            switch (strategy) {
            case MigrationStrategy::Link:
                if (!Common::FS::CreateSymlink(source, destination)) {
                    throw std::runtime_error("The old directory could not be linked.");
                }
                break;
            case MigrationStrategy::Move:
                fs::rename(source, destination);
                break;
            case MigrationStrategy::Copy:
                fs::copy(source, destination,
                         fs::copy_options::recursive | fs::copy_options::copy_symlinks);
                break;
            default:
                throw std::runtime_error("Unknown migration strategy.");
            }
        }

        // Never clear a cache shared through a linked migration or a custom shader path.
        if (clear_shader_cache && strategy != MigrationStrategy::Link &&
            fs::weakly_canonical(shader_dir) == fs::absolute(shader_dir).lexically_normal() &&
            (shader_dir == eden_dir / "shader" || shader_dir == cache_dir / "shader")) {
            preserve(shader_dir);
            fs::create_directories(shader_dir);
        }
    } catch (const std::exception& exception) {
        emit error(tr("Migration stopped: %1\n\nExisting destination data was preserved in "
                      "any .pre-grid0-migration-* directories beside the original location. "
                      "Keep those directories.")
                       .arg(QString::fromUtf8(exception.what())));
        return;
    }

    if (strategy == MigrationStrategy::Link) {
        success_text.append(tr("\n\nData and settings are shared with %1.").arg(selected_emu.name()));
    }
    emit finished(success_text, Common::FS::PathToUTF8String(legacy_user_dir));
}
