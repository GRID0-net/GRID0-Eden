#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

"""Stage a Windows release and verify its executable, DLL and Qt plugin imports."""

import argparse
from collections import deque
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from package_metadata import read_git_metadata


SYSTEM_DLLS = set("""
advapi32.dll authz.dll avrt.dll bcrypt.dll bcryptprimitives.dll cfgmgr32.dll combase.dll comctl32.dll
comdlg32.dll crypt32.dll cryptbase.dll d2d1.dll d3d9.dll d3d11.dll d3d12.dll
d3dcompiler_47.dll dbgcore.dll dbghelp.dll dcomp.dll dinput8.dll dnsapi.dll dwmapi.dll
dwrite.dll dxgi.dll gdi32.dll gdi32full.dll hid.dll imm32.dll iphlpapi.dll kernel32.dll
kernelbase.dll ksuser.dll ktmw32.dll mpr.dll msimg32.dll msvcrt.dll msvcp_win.dll mswsock.dll ncrypt.dll
netapi32.dll normaliz.dll ntdll.dll ole32.dll oleacc.dll oleaut32.dll opengl32.dll
powrprof.dll propsys.dll psapi.dll rpcrt4.dll secur32.dll setupapi.dll shell32.dll
shlwapi.dll shcore.dll ucrtbase.dll uiautomationcore.dll urlmon.dll user32.dll
userenv.dll uxtheme.dll version.dll win32u.dll windowscodecs.dll winhttp.dll
wininet.dll winmm.dll winnsi.dll winspool.drv winsta.dll wintrust.dll wlanapi.dll
ws2_32.dll wtsapi32.dll
""".split())


def inspect_pe(objdump, path):
    result = subprocess.run([objdump, "-p", str(path)], check=True, capture_output=True,
                            text=True).stdout
    if "file format pei-x86-64" not in result:
        raise RuntimeError(f"Not a Windows x86_64 PE image: {path}")
    return re.findall(r"DLL Name:\s*(\S+)", result)


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    default_build = "windows-mingw" if os.name == "nt" else "windows-x86_64"
    parser.add_argument("--build-dir", type=Path, default=root / "build" / default_build)
    parser.add_argument("--sysroot", type=Path, default=os.environ.get("EDEN_CROSS_SYSROOT"))
    parser.add_argument("--objdump", default=shutil.which("x86_64-w64-mingw32-objdump") or
                        shutil.which("objdump") or "x86_64-w64-mingw32-objdump")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--zip", action="store_true")
    args = parser.parse_args()
    if not args.sysroot or not args.sysroot.is_dir():
        parser.error("Supply --sysroot or EDEN_CROSS_SYSROOT for the Windows SDK")
    output = (args.output or args.build_dir / "package/eden-grid0-windows-x86_64").resolve()
    if output.exists() or (args.zip and Path(str(output) + ".zip").exists()):
        parser.error(f"Output already exists: {output}")
    revision, dirty = read_git_metadata(root)
    binaries = sorted((args.build_dir / "bin").glob("*.exe"))
    if not any(path.name == "eden.exe" for path in binaries):
        parser.error("Build eden.exe before packaging")
    sdk = args.sysroot.resolve()
    dlls = {path.name.lower(): path for path in (sdk / "bin").glob("*.dll")}
    plugins = next((path for path in [sdk / "lib/qt6/plugins", sdk / "share/qt6/plugins",
                                      sdk / "plugins"]
                    if (path / "platforms/qwindows.dll").is_file()), None)
    if plugins is None:
        parser.error(f"Windows Qt platform plugin missing below {sdk}")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".windows-stage-", dir=output.parent) as temporary:
        stage = Path(temporary) / output.name
        stage.mkdir()
        pending = deque()
        for binary in binaries:
            target = stage / binary.name
            shutil.copy2(binary, target)
            pending.append(target)
        for category in ["platforms", "styles", "imageformats", "iconengines", "tls",
                         "networkinformation"]:
            for plugin in sorted((plugins / category).glob("*.dll")):
                target = stage / "plugins" / category / plugin.name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(plugin, target)
                pending.append(target)
        imports = {}
        copied = set()
        unresolved = set()
        while pending:
            binary = pending.popleft()
            dependencies = inspect_pe(args.objdump, binary)
            imports[str(binary.relative_to(stage))] = dependencies
            for name in dependencies:
                key = name.lower()
                if key in SYSTEM_DLLS or key.startswith(("api-ms-win-", "ext-ms-win-")):
                    continue
                if key in copied:
                    continue
                source = dlls.get(key)
                if source is None:
                    unresolved.add(f"{binary.name}: {name}")
                    continue
                target = stage / source.name
                shutil.copy2(source, target)
                copied.add(key)
                pending.append(target)
        if unresolved:
            raise RuntimeError("Unresolved Windows imports:\n" + "\n".join(sorted(unresolved)))
        (stage / "qt.conf").write_text("[Paths]\nPlugins=plugins\nTranslations=translations\n")
        shutil.copy2(root / "LICENSE.txt", stage / "LICENSE.txt")
        shutil.copytree(root / "LICENSES", stage / "licenses/eden")
        license_paths = [sdk / "share/licenses"]
        if len(sdk.parents) > 2:
            license_paths.append(sdk.parents[2] / "share/licenses")
        sdk_licenses = next((path for path in license_paths if path.is_dir()), None)
        if sdk_licenses is not None:
            shutil.copytree(sdk_licenses, stage / "licenses/sdk")
        cache = (args.build_dir / "CMakeCache.txt").read_text()
        sources = re.findall(r"^CPM_PACKAGE_(.+)_SOURCE_DIR:INTERNAL=(.+)$", cache, re.MULTILINE)
        sources.append(("dynarmic", str(root / "src/dynarmic")))
        for name, source in sources:
            source = Path(source)
            for pattern in ["LICENSE*", "COPYING*", "NOTICE*"]:
                for notice in source.glob(pattern):
                    target = stage / "licenses/sources" / name / notice.name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    if notice.is_dir():
                        shutil.copytree(notice, target, dirs_exist_ok=True)
                    else:
                        shutil.copy2(notice, target)
        shutil.copy2(root / "cpmfile.json", stage / "cpmfile.json")
        versions = {}
        for package in ["Qt6Core", "openssl", "sdl3", "libzstd"]:
            pc = sdk / "lib/pkgconfig" / (package + ".pc")
            if pc.exists():
                match = re.search(r"^Version:\s*(.+)$", pc.read_text(), re.MULTILINE)
                if match:
                    versions[package] = match.group(1)
        (stage / "build-info.json").write_text(json.dumps({
            "target": "windows-x86_64",
            "configuration": "Release",
            "base_revision": revision,
            "uncommitted_changes": dirty,
            "sdk_versions": versions,
        }, indent=2) + "\n")
        shutil.copy2(root / "docs/GRID0.md", stage / "GRID0.md")
        (stage / "README.txt").write_text(
            "Eden GRID0+ for Windows x86_64\n\n"
            "Extract the entire folder and run eden.exe. Keep the DLLs, plugins and qt.conf\n"
            "beside the executable. Windows 10 or later and a current Vulkan GPU driver\n"
            "are required. Configure your account from GRID0+ > Account and server,\n"
            "select your active network adapter and clear Enable Airplane Mode.\n\n"
            "This is a cross-built test release. Windows gameplay and live GRID0+ online\n"
            "services require validation on Windows. No keys, firmware, games or account\n"
            "credentials are included. See GRID0.md for build and port details.\n"
        )
        (stage / "dll-imports.json").write_text(json.dumps(imports, indent=2) + "\n")
        hashes = []
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                digest = hashlib.sha256(path.read_bytes()).hexdigest()
                hashes.append(f"{digest}  {path.relative_to(stage).as_posix()}")
        (stage / "SHA256SUMS").write_text("\n".join(hashes) + "\n")
        shutil.move(stage, output)
    print(f"Staged {len(imports)} Windows PE images; all imports resolved: {output}")
    if args.zip:
        archive = Path(shutil.make_archive(str(output), "zip", output.parent, output.name))
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        archive.with_suffix(".zip.sha256").write_text(f"{digest}  {archive.name}\n")
        print(archive)


if __name__ == "__main__":
    main()
