#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)

case "$(uname -s)" in
    MINGW*|MSYS*)
        case "${MSYSTEM:-}" in
            MINGW64|UCRT64) ;;
            *) echo 'Open the MSYS2 UCRT64 or MINGW64 shell first.' >&2; exit 1 ;;
        esac
        cd "$project_root"
        cmake --preset windows-mingw -DCMAKE_PREFIX_PATH="$MINGW_PREFIX"
        cmake --build --preset windows-mingw --target yuzu yuzu-cmd yuzu_room_standalone "$@"
        exit
        ;;
    Linux*) ;;
    *) echo 'Use build_windows.ps1 on Windows, or this script on Linux or MSYS2.' >&2; exit 1 ;;
esac

export PATH="$project_root/.cache/windows-sdk/usr/bin:$PATH"
export EDEN_CROSS_SYSROOT="${EDEN_CROSS_SYSROOT:-$project_root/.cache/windows-sdk/usr/x86_64-w64-mingw32/sys-root/mingw}"
export EDEN_QT_HOST_PATH="${EDEN_QT_HOST_PATH:-/usr}"

cd "$project_root"
cmake --preset windows-x86_64 \
    -DYUZU_USE_BUNDLED_FFMPEG=ON \
    -DYUZU_USE_BUNDLED_SDL3=OFF \
    -DYUZU_USE_BUNDLED_OPENSSL=OFF \
    -DYUZU_TESTS=OFF -DENABLE_WERROR=OFF \
    -DVulkanHeaders_FORCE_BUNDLED=ON -DVulkanUtilityLibraries_FORCE_BUNDLED=ON
cmake --build --preset windows-x86_64 --target yuzu yuzu-cmd yuzu_room_standalone "$@"
