#!/bin/sh
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cache="$project_root/.cache/appimage"
mkdir -p "$cache"

# Official type2-runtime 20251108; the ARM64 runtime supports 16 KiB pages.
for arch in aarch64 x86_64; do
    case "$arch" in
        aarch64) digest=00cbdfcf917cc6c0ff6d3347d59e0ca1f7f45a6df1a428a0d6d8a78664d87444 ;;
        x86_64) digest=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d ;;
    esac
    target="$cache/runtime-$arch"
    if ! printf '%s  %s\n' "$digest" "$target" | sha256sum --check --status 2>/dev/null; then
        curl --fail --location "https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-$arch" -o "$target.download"
        printf '%s  %s\n' "$digest" "$target.download" | sha256sum --check
        mv -- "$target.download" "$target"
    fi
    chmod +x "$target"
done

digest=aa154fc9070614bbe7921f89db11efd1dba7a1f3a41685958110e2230f9c0ca1
target="$cache/runtime-LICENSE"
if ! printf '%s  %s\n' "$digest" "$target" | sha256sum --check --status 2>/dev/null; then
    curl --fail --location https://raw.githubusercontent.com/AppImage/type2-runtime/dd6cebe/LICENSE -o "$target.download"
    printf '%s  %s\n' "$digest" "$target.download" | sha256sum --check
    mv -- "$target.download" "$target"
fi
