# GRID0+ fork

This port follows [GRID0-citron](https://github.com/GRID0-net/GRID0-citron) at
`f95abb664890463aa21ecc3e1470cfb6da0871b5`. It carries the GRID0 account login, Nintendo DNS
redirection, secondary
NAT server, SSL CA trust, BSD eventfd and deferred polling, friend records and
presence, friend management window, BCAT delivery, and Splatoon 3 IPS patch loader
and downloader. The original copyright and license notices are retained.

The native macOS frontend, Metal renderer and Citron release updater are not part
of this port. Eden retains its own frontend, Vulkan renderer and updater.

## Account and patches

Open **GRID0+ → Account and server…**, then the Network page. Enter the username
and password supplied by the GRID0+ registration bot. Select your active network
adapter and clear **Enable Airplane Mode** for in-game connections. The server and secondary
NAT address default to the values from GRID0-citron. A custom CA bundle can be
selected for another server; an empty bundle uses the GRID0 CA.

The same login feeds the guest account service, friends/presence and BCAT. Open
**GRID0+ → Friends…** or press **Ctrl+Shift+F** to manage friends. Network
diagnostics can be enabled from the GRID0+ menu for connection troubleshooting.

On startup, Eden checks the newest `patches-<serial>` release from
[GRID0plus-cfw](https://github.com/GRID0-net/GRID0plus-cfw). It asks before installing
changes, checks each downloaded file against the manifest's SHA-256, and restarts
after installation. A manual check is also available in the GRID0+ menu.
Patches are stored below Eden's user directory in `grid0plus/patches`, and are
matched to executable build IDs. No patch binaries are embedded in this fork.
An existing patch set remains usable offline. CA changes take effect after a restart.

To disable redirection, clear both the private server address and login server.
These settings, including the password, are saved in Eden's configuration file.

## Linux

For the normal build steps, see [Build.md](Build.md).

Install the normal [Linux development dependencies](Deps.md), including Qt Charts,
Qt private headers, glslangValidator and FFmpeg development libraries. Then run:

```sh
cmake --preset linux -DYUZU_TESTS=ON
cmake --build --preset linux
cmake --build build/linux --target tests_grid0
ctest --preset linux -R grid0_foundations
```

The aarch64 ELF linker alignment is 64 KiB, allowing loading on 4, 16 and 64 KiB
Linux kernels. Switch guest pages remain 4 KiB.

On larger host pages, complete groups of guest pages use shared native mappings
only when their backing offsets agree. Host permissions are the intersection of
the guest permissions. Fragmented groups and partial unmaps remain inaccessible
to native code, preserving the individual guest mappings in the software table.

Selecting NCE on a larger-page host enables a hybrid execution path. Arithmetic
and compatible memory accesses run natively. Faulting subpage accesses are stepped
through Dynarmic outside the signal handler. Syscalls, emulated system registers
and exclusive instructions use traps and the original guest instruction. This
keeps NCE available without rounding away guest aliases or protections. Its
game compatibility and performance require testing on the target system.

A key-free NRO is provided for CPU smoke testing on arm64 Linux:

```sh
python3 tools/grid0/run_nce_smoke.py build/linux/bin/eden-cli
```

The runner uses temporary settings, selects NCE and the Null renderer, and checks
both the pass marker and that the arithmetic loop avoids software stepping. It exercises native
arithmetic, TLS, counters, exclusive operations and memory accesses in a host page
with mixed guest permissions. It does not exercise a commercial game or GPU.
The NRO can also be generated separately with
`python3 tools/grid0/build_nce_smoke.py build/grid0-nce-smoke.nro`.

The HDR upload sanitizing fix from GRID0-citron is enabled by default. Its existing
`CITROSIS_TC_SANITIZE=0` override disables sanitizing for comparison. The existing
`CITROSIS_TC_NO_GC` and `CITROSIS_BC_NO_GC` diagnostic overrides are also retained.

## Cross compilation from Linux

The presets configure cross builds; they do not supply cross compilers or target
SDKs. Install compilers that run on your Linux host, a matching x86_64 target
sysroot with Eden's dependencies, and native Qt build tools matching the target
Qt version. Host libraries cannot be linked into an executable for a different target.

For Linux, the default compiler names are `x86_64-linux-gnu-gcc` and
`x86_64-linux-gnu-g++`. For Windows they are `x86_64-w64-mingw32-gcc`,
`x86_64-w64-mingw32-g++` and `x86_64-w64-mingw32-windres`. Override
`EDEN_CROSS_TRIPLE` for another GCC target prefix.

```sh
export EDEN_QT_HOST_PATH=/usr
export EDEN_CROSS_SYSROOT=/absolute/path/to/linux-x86_64-sysroot
cmake --preset linux-x86_64
cmake --build --preset linux-x86_64

export EDEN_CROSS_SYSROOT=/absolute/path/to/windows-x86_64-sysroot
cmake --preset windows-x86_64
cmake --build --preset windows-x86_64
```

Target package discovery is restricted to the supplied sysroot. The pkg-config
wrapper clears host package search paths. Target `.pc` files must describe paths
relative to the target filesystem. Supply `Qt6_DIR` or `CMAKE_PREFIX_PATH` if Qt
is outside its conventional location within the sysroot. Windows uses MinGW Qt,
not MSVC Qt. OpenGL is disabled in the cross presets; Vulkan remains available.

Cross-built dependencies and executables still need validation on their target
platforms. Splatoon 3 rendering, login, friends joining, NAT traversal, BCAT and
matches also require a game dump and live GRID0 test accounts.

### Windows cross-build from Fedora

The helper scripts download Fedora's MinGW cross-compiler and Windows Qt
libraries into `.cache/windows-sdk`. They do not install system packages. Native
CMake, Ninja, Python, pkg-config, rpm2cpio, cpio, glslangValidator and Qt build tools
are required. The native Qt tools must match the target Qt version.

```sh
tools/grid0/bootstrap_windows_sdk.sh
tools/grid0/build_windows.sh

export PATH="$PWD/.cache/windows-sdk/usr/bin:$PATH"
export EDEN_CROSS_SYSROOT="$PWD/.cache/windows-sdk/usr/x86_64-w64-mingw32/sys-root/mingw"
python3 tools/grid0/package_windows.py --zip
```

Executables are written to `build/windows-x86_64/bin`. The packaging helper stages
them with their DLL dependencies, Qt plugins, license files, an import audit and
SHA-256 checksums under `build/windows-x86_64/package`. It refuses unresolved
non-system DLL imports and checks that every PE image targets x86_64. This is a
Vulkan build for Windows 10 or later; it requires a current GPU driver. Extract
the complete package and run `eden.exe` with its accompanying DLLs and plugins.

The import audit checks packaging completeness. Actual startup, graphics and
GRID0+ online behavior still require testing on a Windows machine.

## Local validation

On the arm64 Asahi host with 16 KiB pages, the GUI and CLI build successfully.
The NCE smoke runner passes, including native arithmetic followed by trapped TLS,
counter and exclusive operations and mixed-permission memory accesses. The
`grid0_foundations` suite passes 86 assertions across 11 cases, covering host-page
mapping/protection/aliases, native executable mappings, HDR sanitizing and DNS
redirection boundaries.

The full existing suite reports eight failures in the unchanged GPU memory-tracker
tests: their `UpdatePagesCachedBatch` fixture is an empty stub, so expected cache
notifications are absent. Eight raw 4 KiB host-view tests are skipped on this host;
the new subpage suite covers the larger-page mapping behavior separately.

The CLI's SDL callback state, continuation results and input lifetime were repaired
to allow the headless NRO to run and exit cleanly. No commercial game or live GRID0
account was available for validation. Windows x86_64 GUI, CLI and room-server
executables cross-compile with MinGW. The package helper checks their architecture
and DLL imports; startup and gameplay still need Windows testing. The Linux
x86_64 cross preset has not been built yet.
