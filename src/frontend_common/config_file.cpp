// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend_common/config_file.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
std::optional<std::string> ReadFile(const std::filesystem::path& path) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec == std::errc::no_such_file_or_directory ||
        (!ec && status.type() == std::filesystem::file_type::not_found)) {
        return std::nullopt;
    }
    if (ec || !std::filesystem::is_regular_file(status)) {
        throw ConfigFileError("Settings path is not a readable regular file.");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw ConfigFileError("Settings file could not be opened for reading.");
    }
    std::string contents{std::istreambuf_iterator<char>(input), {}};
    if (input.bad()) {
        throw ConfigFileError("Settings file could not be read completely.");
    }
    return contents;
}
} // namespace

std::string ConfigFile::Load(const std::filesystem::path& filename) {
    loaded = false;
    backed_up = false;
    path = filename;
    previous = ReadFile(path);
    if (previous && previous->find('\0') != std::string::npos) {
        throw ConfigFileError("Settings file contains invalid data (expected UTF-8).");
    }
    loaded = true;
    return previous.value_or("");
}

void ConfigFile::Save(const std::string& contents) {
    namespace fs = std::filesystem;
    if (!loaded) {
        throw ConfigFileError("Settings were not loaded successfully; refusing to replace them.");
    }
    if (ReadFile(path) != previous) {
        throw ConfigFileError("Settings changed on disk; refusing to overwrite another process.");
    }
    if (!backed_up && previous) {
        auto backup = path;
        backup += ".pre-grid0.bak";
        std::error_code ec;
        fs::copy_file(path, backup, fs::copy_options::skip_existing, ec);
        if (ec) {
            throw ConfigFileError("Settings backup could not be created; refusing to replace them.");
        }
        backed_up = true;
    }

    static std::atomic<unsigned long long> sequence{};
    auto temporary = path;
    temporary += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                 "-" + std::to_string(sequence++);
    try {
        std::ofstream output(temporary, std::ios::binary | std::ios::out);
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        output.close();
        if (!output) {
            throw ConfigFileError("Settings could not be written completely.");
        }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw ConfigFileError("Settings file could not be replaced.");
        }
#else
        fs::rename(temporary, path);
#endif
        previous = contents;
    } catch (...) {
        std::error_code ec;
        fs::remove(temporary, ec);
        throw;
    }
}
