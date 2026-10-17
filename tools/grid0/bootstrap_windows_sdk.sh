#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
package_dir="$project_root/.cache/windows-rpms"
sdk_dir="$project_root/.cache/windows-sdk"
mkdir -p "$package_dir" "$sdk_dir"
download_dir=$(mktemp -d "$project_root/.cache/windows-download.XXXXXX")
trap 'rm -rf -- "$download_dir"' EXIT

# Fedora packages provide an ARM-native compiler and Windows target libraries.
dnf download --resolve --destdir "$download_dir" \
    mingw64-gcc-c++ mingw64-qt6-qtbase mingw64-qt6-qtcharts mingw64-qt6-qttools \
    mingw64-qt6-qtimageformats mingw64-qt6-qtsvg mingw64-openssl mingw64-zlib \
    mingw64-zstd mingw64-libusb1 mingw64-SDL3

for package in "$download_dir"/mingw*.rpm; do
    rpm2cpio "$package" | (cd "$sdk_dir" && cpio -idmu --quiet)
    cp -- "$package" "$package_dir/"
done

python3 - "$sdk_dir/usr/x86_64-w64-mingw32/sys-root/mingw" <<'PY'
from pathlib import Path
import sys

sdk = Path(sys.argv[1]).resolve()
prefix = '/usr/x86_64-w64-mingw32/sys-root/mingw'
for path in sdk.rglob('*'):
    if path.is_file() and path.suffix in {'.pc', '.cmake', '.prl', '.pri'}:
        original = path.read_text()
        replacement = '' if path.suffix == '.pc' else str(sdk)
        relocated = original.replace(str(sdk), prefix).replace(prefix, replacement)
        if original != relocated:
            path.write_text(relocated)
PY

printf 'Windows SDK ready: %s\n' "$sdk_dir"
printf 'Build with: %s/tools/grid0/build_windows.sh\n' "$project_root"
