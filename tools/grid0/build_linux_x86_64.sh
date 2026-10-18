#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
export EDEN_CROSS_SYSROOT=${EDEN_CROSS_SYSROOT:-"$project_root/.cache/linux-x86_64-sdk"}
export EDEN_QT_HOST_PATH=${EDEN_QT_HOST_PATH:-/usr}
if [[ ! -d "$EDEN_CROSS_SYSROOT/usr/include" ]]; then
    printf 'Set EDEN_CROSS_SYSROOT or run tools/grid0/bootstrap_linux_x86_64_sdk.sh first.\n' >&2
    exit 1
fi

cd -- "$project_root"
cmake --preset linux-x86_64 \
    -DEDEN_CROSS_USE_CLANG=ON -DEDEN_CROSS_TRIPLE=x86_64-redhat-linux-gnu \
    -DYUZU_USE_BUNDLED_FFMPEG=ON -DYUZU_USE_BUNDLED_SDL3=OFF \
    -DVulkanHeaders_FORCE_BUNDLED=ON -DVulkanUtilityLibraries_FORCE_BUNDLED=ON
cmake --build --preset linux-x86_64 --target yuzu yuzu-cmd yuzu_room_standalone "$@"
