# Building the PCSX2 core

This repository builds PCSX2 (the PlayStation 2, and the NAMCO System 246,
System 256 and Super System 256 arcade boards) as a sandboxed guest for
Chimera. The result is one file, `pcsx2.chimeraCore`, which Chimera loads.
The steps below are the ones `.github/workflows/chimera.yml` runs on a fresh
clone on a public Ubuntu runner. Cores are built on Linux; the same package
file runs on Linux and on Windows, because the guest inside it is run by
Chimera's sandbox (miniBox) on either.

Names used below:

- `<chimera>` - a checkout of https://github.com/ToolAssisted-run/chimera.
- `<minibox>` - `<chimera>/extern/chimera-common-minibox`, the miniBox
  submodule: the sandbox host and the guest toolchain.

The commands use two shell variables for them. Both must be absolute paths
(CI passes `$PWD/chimera-checkout/...`). Commands run from the root of this
repository unless they say otherwise.

```sh
chimera=/absolute/path/to/chimera
mb="$chimera/extern/chimera-common-minibox"
```

## Requirements

**Operating system.** CI builds on GitHub's `ubuntu-latest` runner, x86-64.

**Packages for the core and the core gate** (workflow job `core-gate`):

```sh
sudo apt-get update
sudo apt-get install -y --no-install-recommends meson ninja-build build-essential python3 bison flex pkg-config python3-mako python3-packaging
```

**Packages for the frontend gate** (workflow job `frontend-gate`, which also
builds Chimera itself):

```sh
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  meson ninja-build build-essential cmake pkg-config python3 bison flex \
  mono-complete xvfb \
  libgl1-mesa-dev libx11-dev libxext-dev libasound2-dev python3-mako python3-packaging
```

**Toolchains.**

- C and C++: the gcc and g++ that `build-essential` installs. The workflow
  pins no compiler version.
- .NET SDK 8.0, for the frontend gate only. CI uses `actions/setup-dotnet@v4`
  with `dotnet-version: '8.0'`. Chimera's README gives the manual equivalent:
  `curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 8.0`.
  It also says distro-built SDKs omit the WindowsDesktop targets the frontend
  needs.
- Mono and Xvfb (`mono-complete`, `xvfb`), for the frontend gate only.
- No Rust.

**What the scripts fetch or build themselves.**

- `waterbox/setup-mesa.sh` downloads one file:
  `https://archive.mesa3d.org/mesa-24.0.9.tar.xz`. It checks it against the
  SHA-256 `51aa686ca4060e38711a9e8f60c8f1efaa516baf411946ed7f2c265cd582ca4c`
  and stops if it does not match. The tarball is kept in `build/deps/` (or
  `$CHIMERA_DEPS_DIR`) and unpacked into `build/mesa/`. The script calls
  `curl`, `sha256sum` and `tar`; the workflow installs none of them and uses
  the runner's.
- From that tarball it cross-builds Mesa for the guest: the softpipe driver
  (`-Dgallium-drivers=swrast`) behind the gallium OSMesa front end, static,
  with LLVM disabled. zlib and expat come from Mesa's own Meson wraps
  (`-Dforce_fallback_for=zlib,expat`). The output is `build/mesa/build-guest2/`.
- If the system has no `meson` with the Python modules `mako` and `packaging`,
  the same script makes a private Python venv at
  `$HOME/.cache/chimera-mesa-build-venv` (or `$MESA_BUILD_VENV`) and installs
  `meson ninja mako packaging` into it with pip. With the apt packages above
  the system meson is used and no venv is made.
- miniBox builds the guest C and C++ toolchain from the Chimera submodule.
- Everything else is source: this repository and its four submodules,
  `extern/pcsx2`, `extern/zlib`, `extern/zstd` and `extern/lz4`. PCSX2's
  per-title database and its GLSL shaders are compiled into the core from the
  `extern/pcsx2` submodule at build time (`waterbox/gen-gamedb.py`,
  `waterbox/gen-shaders.py`).

## Get the sources

CI checks this repository out with `actions/checkout@v6` and
`submodules: true`. By hand:

```sh
git clone https://github.com/ToolAssisted-run/chimera-core-pcsx2.git
cd chimera-core-pcsx2
git submodule update --init
```

Chimera, with the miniBox submodule. CI checks out Chimera's `main` branch
(`CHIMERA_REF: main`) into `chimera-checkout/` inside this repository's
workspace:

```sh
git clone https://github.com/ToolAssisted-run/chimera.git "$chimera"
git -C "$chimera" submodule update --init extern/chimera-common-minibox
```

That is enough to build the core, the package and the core gate. The frontend
gate builds Chimera too, and for that CI checks Chimera out with
`submodules: recursive`:

```sh
git -C "$chimera" submodule update --init --recursive
```

Where the scripts look when they are not told:

| Script | Option | Otherwise |
| --- | --- | --- |
| `meson.build` | `-Dminibox_dir=<minibox>` | `../chimera/extern/chimera-common-minibox` beside this repository, else an error |
| `waterbox/setup-mesa.sh`, `waterbox/setup-guest.sh` | `-m <minibox>` | `$MINIBOX_DIR`, else `$HOME/chimera/extern/chimera-common-minibox` |
| `waterbox/build-package.sh` | `-r <chimera>`, `-m <minibox>` | Chimera: `../chimera`, else `$HOME/chimera`. miniBox: `$MINIBOX_DIR`, else `<chimera>/extern/chimera-common-minibox` |
| `waterbox/tests/run-frontend.sh` | `--chimera-root <chimera>` | `../chimera`, else `$HOME/chimera` |

The defaults do not all agree, so pass the paths, as CI does.

## Build miniBox

The host library, and the C++ guest toolchain:

```sh
[ -f "$mb/build/meson-linux/build.ninja" ] || meson setup "$mb/build/meson-linux" "$mb"
meson compile -C "$mb/build/meson-linux"
[ -f "$mb/build/meson-cpp/build.ninja" ] || meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true
meson compile -C "$mb/build/meson-cpp"
```

`build/meson-cpp/guest-sysroot` is the sysroot `setup-mesa.sh` and
`setup-guest.sh` require. `run-wbx` links
`build/meson-cpp/source/host/libminiboxhost.so`. CI keeps both build
directories in an `actions/cache@v4` cache; on your machine they simply stay.

## Build the core

### Patches

`extern/pcsx2` is pinned to pristine upstream. The 26 numbered patch files in
`patches/` (two of them share the number 0011) are applied to it by
`waterbox/apply-patches.sh`, in file-name order. There is no separate step:
`meson.build` runs the script at configure time, so both
`meson setup build/meson-native` and `setup-guest.sh` apply them.

The script judges the series as a whole:

- It first applies every patch, in order, to a scratch copy of the touched
  files as the submodule's HEAD has them. If that fails it stops and names the
  patch: the submodule was moved without rebasing the patches.
- If the working tree is pristine it applies the series and prints
  `applied: <patch>` for each.
- If the working tree is exactly what the series leaves behind it prints
  `already applied: all 26 patches` and changes nothing.
- Anything in between is an error that names the files and prints the reset
  command.

### The guest Mesa

The OpenGL renderer runs inside the sandbox against a Mesa softpipe that is
linked into the core. Build it once:

```sh
bash waterbox/setup-mesa.sh -m "$mb"
```

Run it with `bash`, not `sh`: the script uses `set -o pipefail`.

- Options: `-m <miniBox dir>`, `-j N` (default: `nproc`).
- `MESA_TARBALL=<path>` uses a tarball already on the machine. It is still
  checked against the SHA-256.
- `MESA_BUILD_CONFIGURE_ONLY=1` stops after configuring. The script's header
  describes the compile it skips as about 15 minutes.
- If `build/mesa/build-guest2` already holds the archives and the osmesa
  `target.c.o`, the script prints `mesa: already built` and exits.
- Mesa's final shared osmesa library fails to link for the guest. That is
  expected. The core links the static archives and `target.c.o`, and the
  script checks that those exist and ends with `mesa: ready - ...`.

### The native reference

The same curated sources (`waterbox/sources.sh`) built for the host. It is
what the gates compare the sandboxed core against.

```sh
meson setup build/meson-native -Dminibox_dir="$mb"
ninja -C build/meson-native
```

This builds `run-native` (the reference) and `run-wbx` (the driver that runs
`core.wbx` through the miniBox host library). CI configures it without
`-Dmesa_guest_dir`. The frontend gate needs only
`ninja -C build/meson-native run-native`.

Two more options exist for the native build (`meson_options.txt`). CI sets
neither:

- `-Dgl_bridge=true` builds the host half of the GPU bridge
  (`waterbox/gl-host.c`) into `run-wbx`. It needs the EGL library. Two gate
  legs use it.
- `-Djit_rasterizer=true` makes a diagnostic reference that draws the
  software renderer's scanlines with PCSX2's code generator instead of the
  C++ path the core ships. One gate leg compares the two; it looks for that
  build in `build/meson-jit`.

### The guest core

```sh
MINIBOX_DIR="$mb" sh waterbox/setup-guest.sh \
  -- -Dminibox_dir="$mb" -Dmesa_guest_dir="$PWD/build/mesa"
ninja -C build/meson-guest
```

`setup-guest.sh` writes `build/guest-cross.ini` (machine-local paths) and runs
`meson setup build/meson-guest`; if that fails it runs it again with
`--reconfigure`. Arguments after `--` go to meson. The result is
`build/meson-guest/core.wbx`.

`-Dmesa_guest_dir` decides what the core can draw with:

- With it: PCSX2's OpenGL renderer is built and linked against the guest
  Mesa, its shaders are compiled in, and the guest half of Chimera's GPU
  bridge is generated (miniBox's `source/gl/gen-gl-bridge.py`, from
  `waterbox/gl-entry-points.txt`) and compiled in. The core then offers three
  renderers: `software`, `opengl` (softpipe, inside the sandbox) and
  `opengl-hw` (the same renderer with its calls leaving the sandbox to the
  host's GPU through the bridge).
- Without it: only PCSX2's software renderer is built.

## Build the package

```sh
./waterbox/build-package.sh -m "$mb" -r "$chimera"
```

Options: `-m <miniBox dir>` and `-r <chimera root>`. There is no output
directory option.

What it does, in order:

1. Requires the guest Mesa at `build/mesa/build-guest2`. `MESA_GUEST_DIR=<path>`
   names one elsewhere. `MESA_GUEST_DIR=` (set, empty) builds a software-only
   core instead.
2. If `build/meson-guest/build.ninja` does not exist, runs `setup-guest.sh`.
   Then always `ninja -C build/meson-guest core.wbx`, and miniBox's
   `source/guest/check-wbx.sh` on the result.
3. Stages `core.wbx`, `waterbox.config`, `default_keybinds.json`,
   `file_slots.json`, the licence texts (`waterbox/package-licenses.json`)
   and a `build.json` that records the toolchain and the pins.
4. Stamps the version into the staged `waterbox.config`. CI passes
   `CORE_VERSION` (the commit). Without it the stamp is `<commit>+local`,
   with `-dirty` after the commit when `git diff --quiet HEAD` reports
   changes. The applied patch series counts as a change, so a hand build
   normally reads `<commit>-dirty+local`. `versionDate` is the commit's date
   in UTC.
5. Writes `<chimera>/build/Cores/pcsx2.chimeraCore`, replacing the one there.
   The zip is written twice and the two SHA-1s must match; it prints
   `package sha1 ...` and `packaged -> ...`.
6. Removes `<chimera>/build/CoreCache/pcsx2-*`.

The script does not need the native reference. From a built miniBox, the
shortest path to a package is `setup-mesa.sh` and then `build-package.sh`.

## Install it into Chimera

Chimera ships no cores and downloads nothing: it has no network code. A core
is a file a person puts in Chimera's `Cores` folder.

- **A Chimera source checkout.** The cores folder is `<chimera>/build/Cores/`,
  and `build-package.sh -r <chimera>` has already written the package there.
- **A release bundle.** Copy `pcsx2.chimeraCore` into the `Cores` folder
  beside `Chimera.exe`, or into the folder chosen with Change folder... in
  File > Core Manager.
- **Without building.** Download the package from this repository's Releases
  page, https://github.com/ToolAssisted-run/chimera-core-pcsx2/releases, and
  put it in the same folder. CI publishes a rolling `dev` release on every
  green push to `main` and a dated `nightly-YYYY-MM-DD` release from the
  scheduled run, when `main` moved since the last one.

File > Core Manager lists what is in the folder; Refresh List rescans it.

A package's version is the commit it was built from. A hand-built package
carries `+local` and is for testing; Chimera's publishing script refuses to
publish one. A published package is named `pcsx2-<version>.chimeraCore` and
the one built here `pcsx2.chimeraCore`; Chimera identifies a package by its
content, not by its file name.

## Run the gates

A PlayStation 2 has no HLE bios: nothing executes without a real one, and a
bios is somebody's dump. So both gates are bounded by content. With nothing
provided they prove very little, and they say so with SKIP lines. A green CI
run means the core builds, starts and refuses clearly; it does not mean the
machine was tested.

### The core gate

```sh
./waterbox/run-gate.sh
```

Options: `-n <native build dir>` (default `build/meson-native`) and
`-g <guest build dir>` (default `build/meson-guest`). It needs `run-native`,
`run-wbx` and `core.wbx`. It prints one line per check and ends with
`N ok, N failed, N skipped`; it fails when any check failed.

**With nothing provided** (what CI runs), five checks run:

| Check | What it holds |
| --- | --- |
| `keybinds` | every declared control has a default key |
| `arcade:panel-words` | the arcade panel's switch words (`tests/own/test-arcade-panel.cpp`, compiled by the gate with `g++`) |
| `nobios:refuses` | a machine with no bios is refused, and the refusal names the bios, natively and in the sandbox |
| `gamedb:loaded` | the per-title database is compiled in and is not empty, natively and in the sandbox |
| `gamedb:entry` | one known database entry reads back with its fixes in both flavors |

Every other check reports SKIP and says what it would have proven.

**With a bios** - the first `*.bin` in `tests/roms/bios/`, or the file
`$CHIMERA_PS2_BIOS` names - the machine itself is gated: native == sandbox on
video, audio, lag and memory-domain digests (`bios:equivalence`), the
interpreters (`cpu:interpreter`, `cpu:interpreter-rerecord`), turbo, the
deinterlacer, a savestate round-trip around every frame (`bios:savestate`),
native determinism, the memory domains, the picture, input and lag, and the
save-data channel. With `tests/own/padtest.elf` (committed, free to
distribute) it also runs `pad:*` and `gun:bus`, `gun:controls`,
`gun:equivalence`.

**More content, more checks:**

| Checks | Needs, besides a bios |
| --- | --- |
| `disc:boots`, `disc:equivalence` | a disc image: the first `*.bin` or `*.iso` in `tests/roms/` |
| `video:rasterisers` | a native build made with `-Djit_rasterizer=true` in `build/meson-jit`, or `CHIMERA_PS2_JIT_BUILD` |
| `gun:aims` | `PCSX2_GUN_DISC`, a GunCon 2 disc |
| `gpu:picture` | `PCSX2_GFX_DISC` (and `PCSX2_GFX_FRAMES`), `run-wbx` built with `-Dgl_bridge=true`, and a GL context |
| `gpu:bridge` | `run-wbx` built with `-Dgl_bridge=true`, a GL context, and a core built with the guest Mesa |
| `ports:columns`, `gl:rebuild-at-zero` | `<chimera>/build/meson-linux/chimera-run` and the installed `<chimera>/build/Cores/pcsx2.chimeraCore`; found through `$CHIMERA_ROOT`, then `chimera-checkout/`, `../../chimera`, `../chimera`, `$HOME/chimera` |

**The arcade boards** carry their own bios, so their checks do not need the
console's. They run once per board when a folder is named, and SKIP
otherwise:

| Variable | Board |
| --- | --- |
| `PCSX2_S246_ROMS` | System 246 (`s246:*`) |
| `PCSX2_S256_ROMS` | System 256 (`s256:*`) |
| `PCSX2_SS256_ROMS` | Super System 256 (`ss256:*`) |

The folder holds, under exactly these names: `bios.bin` (a COH-H arcade bios,
not a retail dump), `boot.elf` (the boot program), `dongle.*` (the game's
security dongle) and `media.*` (its CD, DVD or hard disk image). `_GAME`,
`_MEDIA` (`cd`, `dvd` or `hdd`) and `_RAM` variables with the same prefix
refine it; the comment at the top of `run-gate.sh` describes them.

### The frontend gate

It runs the package inside Chimera itself, headless under Mono. Build Chimera
first, as the workflow does:

```sh
cd "$chimera"
meson setup build/meson-linux --prefix "$PWD/build" --libdir dll
meson compile -C build/meson-linux
meson install -C build/meson-linux
dotnet build source/gui/Chimera.sln -c Release /nodeReuse:false -p:UseSharedCompilation=false
```

Then, from this repository, with the package installed and `run-native` built:

```sh
./waterbox/tests/run-frontend.sh --chimera-root "$chimera"
```

Options: `--chimera-root <path>` and `--frames N` (default 200). Logs and
dumps go to `waterbox/tests/work/`.

It needs a bios and a disc, found as the core gate finds them. Without both,
its three checks report SKIP and it exits successfully - which is what CI
does. The package is still built and published by that job.

| Check | What it holds |
| --- | --- |
| `disc:frontend` | a disc through Chimera: EE RAM equals the native reference |
| `settings:clock` | a machine-shaping setting (the console's clock) reaches the guest through the frontend |
| `keybinds` | the package's key bindings become the frontend's defaults |

The gate passes the bios file's base name, without `.bin`, as the project's
`bios` setting. Name the dump after the setting value it is, for example
`ps2-0230a-20080220.bin`.

## Files the core needs at run time

Game files, BIOS and firmware are never in this repository or in the package.
The user provides them. `waterbox/file_slots.json` and the `firmware` list in
`waterbox/waterbox.config` are the declarations; this is what they say.

**PlayStation 2** (`machine` = `ps2`, the default)

- Firmware, always required: `bios.bin`, a PlayStation 2 bios dump. There is
  no HLE bios. The `bios` setting names which dump the project is (73
  choices; the default is `ps2-0230a-20080220`), and each is pinned by size
  and SHA-1. A different release is a different machine.
- Disc or program: one file. A DVD or CD image (`.iso`, `.bin`, `.img`,
  `.chd`, `.cso`, `.zso`, `.gz`) or a PS2 executable (`.elf`).
- Save data, optional, up to 3 files: `memcard1.ps2`, `memcard2.ps2` and
  `bios.nvm`, as Emulator > Export Save Data... writes them.

**NAMCO System 246, System 256, Super System 256** (`machine` = `system246`,
`system256`, `system256super`)

- Firmware, required: an arcade bios from a COH-H board (4 choices, pinned by
  size and SHA-1). A retail PlayStation 2 bios will not do.
- Security dongle: one file, the dump of the game's own memory card.
- Game media: one file, the game's CD, DVD or hard disk image.
- Boot program: one file (`.elf` or `.bin`).
- Save data, optional: `sram.bin`, the board's settings memory.

The `renderer` setting has three values. `software` (the default) and
`opengl` run inside the sandbox and are deterministic. `opengl-hw` sends the
OpenGL calls to the host's GPU through Chimera's GPU bridge; it is the fastest
where there is a GPU, and its picture is not deterministic.

## Troubleshooting

- **`guest sysroot not built at .../build/meson-cpp/guest-sysroot`**
  (`setup-mesa.sh`) or **`miniBox C++ guest toolchain missing`**
  (`setup-guest.sh`). Build miniBox's `meson-cpp` with `-Dguest_cpp=true`
  first.
- **`guest Mesa not built at .../build/mesa/build-guest2`**
  (`build-package.sh`). Run `bash waterbox/setup-mesa.sh` first, or set
  `MESA_GUEST_DIR=` (empty) for a software-only core.
- **`mesa: the tarball is not the release this core is pinned to`.** The file
  in `build/deps/` (or `$MESA_TARBALL`) does not have the pinned SHA-256.
  Remove it and run the script again.
- **`mesa: no usable meson - install python3-venv, or meson plus
  python3-mako`.** Install `python3-mako` and `python3-packaging` as CI does.
- **`setup-mesa.sh` fails at once under `sh`.** It needs `bash`.
- **Mesa prints a link error for the shared osmesa library.** Expected; see
  "The guest Mesa" above. What matters is the final `mesa: ready` line.
- **`pass -Dminibox_dir=<miniBox checkout>`** (meson). `meson.build` found no
  miniBox at `../chimera/extern/chimera-common-minibox`. Pass the option.
- **`extern/pcsx2 is partly patched`** (`apply-patches.sh`). A patched file
  was edited or reverted by hand. Turn any edits you want into a patch, then
  run the reset command the script prints.
- **`the series does not apply to the submodule's HEAD`.** The submodule pin
  was moved without rebasing the patches.
- **The package has no OpenGL renderer.** `build-package.sh` configures
  `build/meson-guest` only when it has no `build.ninja`. A guest directory
  configured earlier without `-Dmesa_guest_dir` keeps that choice. Remove
  `build/meson-guest`; `build-package.sh` then configures it with the guest
  Mesa.
- **`gpu:bridge` or `gpu:picture` says `run-wbx was built without the
  bridge's host half`.** Configure the native build with `-Dgl_bridge=true`.
- **`gpu:bridge` says `core.wbx has no SetGpuBridge`.** The guest was built
  without a guest Mesa, so without the OpenGL renderer.
- **The gate is green and nearly everything is SKIP.** No bios was found. Put
  one in `tests/roms/bios/` or set `CHIMERA_PS2_BIOS`. `tests/roms/` is
  ignored by git wholesale, so nothing put there can be committed by
  accident.
- **Checks say `needs chimera-run, a built pcsx2.chimeraCore, a bios and
  padtest.elf`.** Set `CHIMERA_ROOT` to a Chimera checkout whose natives are
  built and where the package is installed.
- **The frontend gate stops with `Chimera not built`, `package not installed`
  or `native reference not built`.** Build Chimera, run `build-package.sh`,
  or build `run-native`, respectively. These are checked before the content
  is looked for.
- **`Xvfb not found (apt install xvfb)`.** The frontend gate starts its own X
  display when `DISPLAY` is not set, and needs `xvfb` for it.
- **`minibox-diag.log` appears.** The sandbox writes it when a guest faults.
  It is ignored by git.
