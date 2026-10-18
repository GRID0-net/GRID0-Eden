#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

"""Package a Linux build as an AppImage, including Qt and shared libraries."""

import argparse
from collections import deque
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile


MACHINES = {62: "x86_64", 183: "aarch64"}
# The loader can be bundled separately; kernel graphics interfaces stay on the host.
HOST_LIBRARIES = {
    "libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0", "librt.so.1",
    "libresolv.so.2", "libutil.so.1", "ld-linux-x86-64.so.2", "ld-linux-aarch64.so.1",
    "libvulkan.so.1", "libdrm.so.2",
}


def architecture(path):
    with path.open("rb") as stream:
        header = stream.read(20)
    if header[:6] != b"\x7fELF\x02\x01":
        raise RuntimeError(f"Expected a little-endian ELF64 binary: {path}")
    return MACHINES[struct.unpack_from("<H", header, 18)[0]]


def run(*command):
    return subprocess.run(command, check=True, capture_output=True, text=True).stdout


def check_arm64_pages(path):
    for line in run("readelf", "-lW", str(path)).splitlines():
        fields = line.split()
        if fields and fields[0] == "LOAD":
            if (int(fields[2], 16) - int(fields[1], 16)) % 65536:
                raise RuntimeError(f"ELF layout cannot run on 16K/64K ARM64 hosts: {path}")


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=root / "build/linux")
    parser.add_argument("--sysroot", type=Path, default=Path("/"))
    parser.add_argument("--library-dir", action="append", type=Path, default=[])
    parser.add_argument("--qt-plugins", type=Path)
    parser.add_argument("--runtime", type=Path)
    parser.add_argument("--patchelf", default="patchelf")
    parser.add_argument("--bundle-glibc", action="store_true",
                        help="Include the matching libc and loader for builds made on newer distros")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    sdk = args.sysroot.resolve()
    arch = architecture(build / "bin/eden")
    runtime = args.runtime or root / ".cache/appimage" / f"runtime-{arch}"
    if not runtime.is_file() or architecture(runtime) != arch:
        parser.error("Supply a matching AppImage --runtime; see bootstrap_appimage.sh")
    output = (args.output or build / "package" / f"GRID0-Eden-{arch}.AppImage").resolve()
    if output.exists():
        parser.error(f"Output already exists: {output}")
    directories = [path.resolve() for path in args.library_dir]
    directories += [sdk / path for path in [
        "usr/lib64", "lib64", "usr/lib/x86_64-linux-gnu", "lib/x86_64-linux-gnu",
        "usr/lib/aarch64-linux-gnu", "lib/aarch64-linux-gnu", "usr/lib", "lib",
    ] if (sdk / path).is_dir()]
    plugins = args.qt_plugins or next((directory / suffix for directory in directories
        for suffix in ["qt6/plugins", "qt/plugins"]
        if (directory / suffix / "platforms/libqxcb.so").is_file()), None)
    if plugins is None:
        parser.error("Cannot find Qt plugins; supply --qt-plugins")
    # Index extra subdirectories too (e.g. Fedora's libproxy backend).
    libraries = {}
    for directory in directories:
        for path in sorted(directory.rglob("*.so*")):
            if path.is_file():
                libraries.setdefault(path.name, path)
    host_libraries = HOST_LIBRARIES.copy()
    loader = "ld-linux-aarch64.so.1" if arch == "aarch64" else "ld-linux-x86-64.so.2"
    if args.bundle_glibc:
        host_libraries -= {name for name in HOST_LIBRARIES if name.startswith(
            ("libc.", "libm.", "libdl.", "libpthread.", "librt.", "libresolv.", "libutil.", "ld-linux"))}
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".appimage-", dir=output.parent) as temporary:
        stage = Path(temporary) / "AppDir"
        (stage / "usr/bin").mkdir(parents=True)
        (stage / "usr/lib").mkdir()
        pending = deque()

        def copy(source, target):
            if architecture(source) != arch:
                raise RuntimeError(f"Wrong architecture: {source}")
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            pending.append((source, target))

        for name in ["eden", "eden-cli", "eden-room"]:
            copy(build / "bin" / name, stage / "usr/bin" / name)
        for directory in directories:
            for provider in (directory / "ossl-modules").glob("legacy.so"):
                target = stage / "usr/lib/ossl-modules" / provider.name
                if not target.exists():
                    copy(provider, target)
        if args.bundle_glibc:
            copy(libraries[loader], stage / "usr/lib" / loader)
        categories = ["platforms", "imageformats", "iconengines", "tls", "networkinformation",
                      "xcbglintegrations", "wayland-decoration-client",
                      "wayland-graphics-integration-client", "wayland-shell-integration"]
        for category in categories:
            for plugin in sorted((plugins / category).glob("*.so")):
                if category == "platforms" and plugin.name not in {
                    "libqxcb.so", "libqwayland.so", "libqwayland-egl.so",
                    "libqwayland-generic.so", "libqoffscreen.so", "libqminimal.so",
                }:
                    continue
                if category == "imageformats" and plugin.name not in {
                    "libqgif.so", "libqico.so", "libqjpeg.so", "libqsvg.so", "libqwebp.so",
                }:
                    continue
                copy(plugin, stage / "usr/plugins" / category / plugin.name)
        imports = {}
        required_glibc = (0, 0)
        while pending:
            source, target = pending.popleft()
            dynamic = run("readelf", "-d", str(source))
            dependencies = re.findall(r"\(NEEDED\).*?\[(.+?)\]", dynamic)
            imports[str(target.relative_to(stage))] = dependencies
            versions = run("readelf", "--version-info", str(source))
            for major, minor in re.findall(r"GLIBC_(\d+)\.(\d+)", versions):
                required_glibc = max(required_glibc, (int(major), int(minor)))
            for name in dependencies:
                if name in host_libraries or (stage / "usr/lib" / name).exists():
                    continue
                library = libraries.get(name)
                if library is None:
                    raise RuntimeError(f"Unresolved library {name} required by {source}")
                copy(library, stage / "usr/lib" / name)
            relative = os.path.relpath(stage / "usr/lib", target.parent)
            if target.name != loader:
                run(args.patchelf, "--set-rpath", "$ORIGIN/" + relative, str(target))
        if "usr/plugins/platforms/libqxcb.so" not in imports:
            raise RuntimeError("Qt X11 platform plugin missing")
        if arch == "aarch64":
            check_arm64_pages(runtime)
            for name in imports:
                check_arm64_pages(stage / name)
        (stage / "usr/bin/qt.conf").write_text("[Paths]\nPrefix=..\nPlugins=plugins\n")
        if args.bundle_glibc:
            shutil.copy2(stage / "usr/bin/qt.conf", stage / "usr/lib/qt.conf")
        (stage / "AppRun").write_text('''#!/bin/sh
appdir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export QT_PLUGIN_PATH="$appdir/usr/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$appdir/usr/plugins/platforms"
export QT_QPA_PLATFORMTHEME=
export OPENSSL_MODULES="$appdir/usr/lib/ossl-modules"
if [ -z "${SSL_CERT_FILE:-}" ]; then
    for certificates in /etc/ssl/certs/ca-certificates.crt /etc/pki/tls/certs/ca-bundle.crt /etc/ssl/ca-bundle.pem /etc/ssl/cert.pem; do
        if [ -r "$certificates" ]; then
            export SSL_CERT_FILE="$certificates"
            break
        fi
    done
fi
program=eden
case "${1:-}" in
    --cli) program=eden-cli; shift ;;
    --room) program=eden-room; shift ;;
esac
@EXEC@
''')
        launch = ('exec "$appdir/usr/lib/' + loader + '" --argv0 "$appdir/AppRun" '
                  '--library-path "$appdir/usr/lib" '
                  '"$appdir/usr/bin/$program" "$@"' if args.bundle_glibc else
                  'export LD_LIBRARY_PATH="$appdir/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\n'
                  'exec "$appdir/usr/bin/$program" "$@"')
        app_run = stage / "AppRun"
        app_run.write_text(app_run.read_text().replace("@EXEC@", launch))
        (stage / "AppRun").chmod(0o755)
        desktop = (root / "dist/dev.eden_emu.eden.desktop").read_text()
        desktop = desktop.replace("Name=Eden", "Name=GRID0-Eden").replace("TryExec=eden\n", "")
        desktop = desktop.replace("Exec=eden %f", "Exec=AppRun %f")
        (stage / "dev.eden_emu.eden.desktop").write_text(desktop)
        shutil.copy2(root / "dist/dev.eden_emu.eden.svg", stage / "dev.eden_emu.eden.svg")
        (stage / ".DirIcon").symlink_to("dev.eden_emu.eden.svg")
        licenses = stage / "usr/share/licenses/GRID0-Eden"
        shutil.copytree(root / "LICENSES", licenses / "eden")
        shutil.copy2(root / "LICENSE.txt", licenses / "LICENSE.txt")
        runtime_license = runtime.parent / "runtime-LICENSE"
        if runtime_license.is_file():
            shutil.copy2(runtime_license, licenses / "AppImage-runtime-LICENSE")
        for directory in [sdk / "usr/share/licenses", sdk / "usr/share/doc"]:
            if directory.is_dir():
                for package in directory.iterdir():
                    if package.is_dir():
                        for notice in package.iterdir():
                            if notice.is_file() and re.match(r"(?i)(copying|copyright|licen[cs]e|notice)", notice.name):
                                dest = licenses / "system" / package.name / notice.name
                                dest.parent.mkdir(parents=True, exist_ok=True)
                                shutil.copy2(notice, dest)
        cache = (build / "CMakeCache.txt").read_text()
        sources = re.findall(r"^CPM_PACKAGE_(.+)_SOURCE_DIR:INTERNAL=(.+)$", cache, re.MULTILINE)
        sources.append(("dynarmic", str(root / "src/dynarmic")))
        for name, directory in sources:
            for pattern in ["LICENSE*", "COPYING*", "NOTICE*"]:
                for notice in Path(directory).glob(pattern):
                    dest = licenses / "sources" / name / notice.name
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    if notice.is_dir():
                        shutil.copytree(notice, dest, dirs_exist_ok=True)
                    else:
                        shutil.copy2(notice, dest)
        info = {
            "architecture": arch,
            "minimum_glibc": ".".join(map(str, required_glibc)),
            "bundled_glibc": args.bundle_glibc,
            "revision": run("git", "-C", str(root), "rev-parse", "HEAD").strip(),
            "uncommitted_changes": bool(run("git", "-C", str(root), "status", "--porcelain")),
            "runtime_sha256": hashlib.sha256(runtime.read_bytes()).hexdigest(),
            "host_libraries": sorted(host_libraries),
            "imports": imports,
        }
        (stage / "build-info.json").write_text(json.dumps(info, indent=2) + "\n")
        (stage / "README.md").write_text((root / "README.md").read_text())
        hashes = [f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(stage)}"
                  for path in sorted(stage.rglob("*")) if path.is_file()]
        (stage / "SHA256SUMS").write_text("\n".join(hashes) + "\n")
        squash = Path(temporary) / "payload.squashfs"
        subprocess.run(["mksquashfs", str(stage), str(squash), "-root-owned", "-noappend", "-no-xattrs",
                        "-comp", "zstd", "-processors", "2", "-no-progress"], check=True)
        partial = Path(temporary) / output.name
        with partial.open("wb") as stream:
            stream.write(runtime.read_bytes())
            with squash.open("rb") as payload:
                shutil.copyfileobj(payload, stream)
        partial.chmod(0o755)
        partial.rename(output)
        digest = hashlib.sha256(output.read_bytes()).hexdigest()
        output.with_suffix(".AppImage.sha256").write_text(f"{digest}  {output.name}\n")
        output.with_suffix(".AppImage.json").write_text(json.dumps(info, indent=2) + "\n")
        print(f"Packaged {len(imports)} ELF files; glibc {info['minimum_glibc']} " +
              ("included" if args.bundle_glibc else "required on host"))
        print(output)


if __name__ == "__main__":
    main()
