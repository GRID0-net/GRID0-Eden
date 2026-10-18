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

### making an AppImage

want a single file you can share? install `patchelf`, `squashfs-tools` and `curl`,
then package your Linux build:

```sh
./tools/grid0/bootstrap_appimage.sh
python3 tools/grid0/package_linux.py --build-dir build/linux --bundle-glibc
```

you'll get `GRID0-Eden-aarch64.AppImage` or `GRID0-Eden-x86_64.AppImage` in
`build/linux/package`, along with its checksum. the packager follows the build's
architecture, includes Qt and the runtime libraries, and leaves GPU drivers to
the machine running it. it doesnt include games, keys or your account settings.

make it executable and run it:

```sh
chmod +x GRID0-Eden-*.AppImage
./GRID0-Eden-x86_64.AppImage
```

use the `aarch64` file instead if you're on arm64. if FUSE isnt available, run
with `APPIMAGE_EXTRACT_AND_RUN=1` before the filename. `--cli` opens the command
line build and `--room` runs the room server.

`--bundle-glibc` includes the matching C runtime, which helps when you're building
on a newer distro than the people using it. you can leave it out if you're
building on an older distro and want to use the destination machine's glibc.

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

## building releases on GitHub

push these changes to the repo's default branch, then tag the version you want
to build. for example, after committing your changes:

```sh
git tag v0.2.1
git push origin v0.2.1
```

open **Actions**, pick **Release everything**, hit **Run workflow** and enter
that tag. it'll build the Windows x86_64 ZIP and both Linux AppImages, then put
them and their checksums in the same release. it creates the release if needed
and publishes it once the uploads finish. no extra secrets to set up.

there are separate **Release Windows x86_64**, **Release Linux x86_64** and
**Release Linux ARM64** workflows too. use the same tag to add a platform to an
existing release, or rebuild just that package. reruns replace that platform's
files and leave your release notes alone. if you've enabled immutable releases,
use **Release everything** with a new tag so all the files are uploaded before
the release gets locked.

the build packages also stay available as workflow artifacts if a release upload
fails. the workflows only run when you start them yourself.

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
