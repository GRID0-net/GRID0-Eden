# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

set(CMAKE_SYSTEM_NAME Windows)
set(EDEN_CROSS_TRIPLE x86_64-w64-mingw32 CACHE STRING "Target compiler triple")
include("${CMAKE_CURRENT_LIST_DIR}/Grid0Cross.cmake")
find_program(EDEN_CROSS_RC NAMES "${EDEN_CROSS_TRIPLE}-windres" REQUIRED)
set(CMAKE_RC_COMPILER "${EDEN_CROSS_RC}")
