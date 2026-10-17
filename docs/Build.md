# building GRID0-Eden

want to build it yourself? here's how.

clone this repo and open a terminal in its folder. you'll need Git, Python 3,
CMake 3.31 or newer, Ninja and a recent C++ compiler. the first build downloads
some dependencies, so give it a bit of time.

## linux

install the packages for your distro from [the dependencies guide](Deps.md).
make sure you have Qt 6, including Charts and the private development headers,
plus glslangValidator and the FFmpeg development libraries.

then run:

```sh
cmake --preset linux
cmake --build --preset linux
```

your build will be in `build/linux/bin`. run `eden` from there.

this builds for the machine you're using, whether that's x86_64, arm64 or another
supported architecture. CMake picks up the compiler's target, and the emulator
handles the host page size automatically.

if you want to use Clang instead of GCC, pick it when configuring:

```sh
cmake --preset linux -B build/linux-clang -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/linux-clang
```

## windows

### Visual Studio / PowerShell

install Visual Studio or Build Tools with **Desktop development with C++**, the
Windows SDK, CMake and Ninja. you'll also need Git, Python 3 and the
[Vulkan SDK](https://vulkan.lunarg.com/sdk/home#windows).

open **Developer PowerShell** for the target you want to build, then run:

```powershell
.\tools\grid0\build_windows.ps1
```

this uses MSVC and downloads the bundled Qt and library builds. you dont need to
install Qt separately for this preset.

if you'd rather run CMake directly:

```powershell
cmake --preset windows
cmake --build --preset windows --target yuzu yuzu-cmd yuzu_room_standalone
```

you'll find `eden.exe`, `eden-cli.exe` and `eden-room.exe` in `build/windows/bin`.
the compiler environment decides the target, so use an x64 environment for an
x64 build, or an ARM64 one for an ARM64 build. keep those in separate build
directories if you're building both.

### MSYS2

if you prefer GCC, use the [MSYS2 UCRT64 shell](https://www.msys2.org/docs/environments/).
update MSYS2 first, then install the build tools and libraries:

```sh
pacman -Syu
pacman -S --needed git patch \
  "${MINGW_PACKAGE_PREFIX}-"{gcc,cmake,ninja,python,pkgconf,glslang,qt6-base,qt6-charts,qt6-tools,qt6-svg,SDL3,openssl,zlib,zstd,libusb,ffmpeg}
```

if the update asks you to close the terminal, reopen the UCRT64 shell before
continuing. from the repo folder, run:

```sh
./tools/grid0/build_windows.sh
```

these builds go in `build/windows-mingw/bin`. this preset uses the libraries from
your MSYS2 environment, including Qt and FFmpeg, so keep everything in the same
environment.

if you're making a package to share, run this from that same shell:

```sh
python tools/grid0/package_windows.py \
  --build-dir build/windows-mingw --sysroot "$MINGW_PREFIX" --zip
```

the ZIP goes in `build/windows-mingw/package`. it includes the runtime DLLs and
Qt plugins, so keep the whole folder together when you extract it.

## building for a different platform

those instructions build with your current platform's compiler. if you're on
Linux and want a Windows build instead, that's a cross-build:

```sh
./tools/grid0/build_windows.sh
```

that needs a MinGW cross-compiler and a Windows dependency SDK first. on Fedora,
`tools/grid0/bootstrap_windows_sdk.sh` can download them into `.cache` for you.
on other distros, provide your own SDK through `EDEN_CROSS_SYSROOT` and put the
cross-compiler on `PATH`.

the cross-build uses `build/windows-x86_64`, separate from native Windows builds.
there's also a `linux-x86_64` cross preset if you're targeting x86_64 Linux from
another architecture. [GRID0.md](GRID0.md#cross-compilation-from-linux) has
the SDK setup and packaging commands.

## a couple of useful things

builds use two jobs by default. change that if you want:

```sh
cmake --build --preset linux --parallel 4
```

swap `linux` for whichever preset you're using. the PowerShell helper also
accepts `-Jobs 4`.

if you're switching compiler, architecture or MSYS2 environment, use a fresh
build directory. dont reuse a configured directory for a different toolchain.
for example:

```powershell
cmake --preset windows -B build/windows-arm64
cmake --build build/windows-arm64 --target yuzu yuzu-cmd yuzu_room_standalone
```

if CMake cant find Qt, point `CMAKE_PREFIX_PATH` at the matching Qt kit. if it
cant find a library, check the [dependencies guide](Deps.md) for your platform.

for macOS and other platforms, the existing [dependencies](Deps.md) and
[caveats](Caveats.md) still apply. Android has its [own build guide](build/Android.md).
