#!/bin/sh
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
: "${EDEN_CROSS_SYSROOT:?Set EDEN_CROSS_SYSROOT to the target sysroot}"
export PKG_CONFIG_SYSROOT_DIR="$EDEN_CROSS_SYSROOT"
export PKG_CONFIG_LIBDIR="$EDEN_CROSS_SYSROOT/usr/lib/pkgconfig:$EDEN_CROSS_SYSROOT/usr/lib64/pkgconfig:$EDEN_CROSS_SYSROOT/usr/lib/x86_64-linux-gnu/pkgconfig:$EDEN_CROSS_SYSROOT/usr/share/pkgconfig:$EDEN_CROSS_SYSROOT/lib/pkgconfig:$EDEN_CROSS_SYSROOT/share/pkgconfig"
unset PKG_CONFIG_PATH
exec pkg-config "$@"
