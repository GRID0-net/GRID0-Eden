# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

param(
    [ValidateRange(1, 256)]
    [int]$Jobs = 2
)

$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') {
    throw 'Run this helper from Developer PowerShell on Windows.'
}
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'Open Developer PowerShell for the target you want to build, then run this script again.'
}

$projectRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Push-Location $projectRoot
try {
    & cmake --preset windows
    if ($LASTEXITCODE -ne 0) { throw 'Windows configuration failed.' }
    & cmake --build --preset windows --target yuzu yuzu-cmd yuzu_room_standalone --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'Windows build failed.' }
} finally {
    Pop-Location
}
