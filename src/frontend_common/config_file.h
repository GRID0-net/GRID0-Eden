// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

class ConfigFileError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Preserve the loaded file before the first write, and refuse to overwrite external edits.
class ConfigFile {
public:
    std::string Load(const std::filesystem::path& filename);
    void Save(const std::string& contents);

private:
    std::filesystem::path path;
    std::optional<std::string> previous;
    bool loaded = false;
    bool backed_up = false;
};
