#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
package_dir="$project_root/.cache/linux-x86_64-rpms"
sdk_dir="$project_root/.cache/linux-x86_64-sdk"
mkdir -p "$package_dir" "$sdk_dir"
download_dir=$(mktemp -d "$project_root/.cache/linux-download.XXXXXX")
trap 'rm -rf -- "$download_dir"' EXIT

dnf download --forcearch=x86_64 --arch=x86_64,noarch --resolve --alldeps \
    --setopt=install_weak_deps=False --repo=fedora --repo=updates --destdir "$download_dir" \
    gcc-c++ glibc-devel qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtcharts-devel \
    qt6-qtsvg qt6-qtwayland SDL3-devel openssl-devel libzstd-devel zlib-devel \
    libusb1-devel libdrm-devel libva-devel

for package in "$download_dir"/*.rpm; do
    case "$(basename -- "$package")" in filesystem-*|setup-*) continue ;; esac
    rpm2cpio "$package" | (cd "$sdk_dir" && cpio -idmu --no-preserve-owner --no-absolute-filenames --quiet)
    cp -- "$package" "$package_dir/"
done

python3 - "$sdk_dir" <<'PY'
from pathlib import Path
import sys

sdk = Path(sys.argv[1]).resolve()
for path in (sdk / 'usr/lib64/cmake').rglob('*.cmake'):
    original = path.read_text()
    relocated = original.replace(str(sdk), '').replace('/usr/', str(sdk) + '/usr/')
    if original != relocated:
        path.write_text(relocated)
PY

printf 'Linux x86_64 SDK ready: %s\n' "$sdk_dir"
printf 'Build with: %s/tools/grid0/build_linux_x86_64.sh\n' "$project_root"
