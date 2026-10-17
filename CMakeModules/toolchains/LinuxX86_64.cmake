# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

set(CMAKE_SYSTEM_NAME Linux)
set(EDEN_CROSS_TRIPLE x86_64-linux-gnu CACHE STRING "Target compiler triple")
include("${CMAKE_CURRENT_LIST_DIR}/Grid0Cross.cmake")
set(CMAKE_SYSROOT "${EDEN_CROSS_SYSROOT}")
