# AGENTS.md - PCSX2 core for Chimera

This repository builds PCSX2 (PlayStation 2, and the NAMCO System 246, 256
and Super 256 arcade boards) as a sandboxed guest for Chimera, a frontend for
tool-assisted speedruns. It produces one file, `pcsx2.chimeraCore`, which
Chimera loads from its `Cores` folder. The same sources are also built
natively as a reference, and the gate holds the two byte-identical.
`.github/workflows/chimera.yml` is the authoritative build recipe;
`docs/BUILDING.md` explains it step by step.

## Layout

- `extern/pcsx2` - upstream PCSX2, a pinned submodule. Never edited in place.
- `extern/zlib`, `extern/zstd`, `extern/lz4` - pinned submodules the core links.
- `patches/` - 26 numbered patch files applied to `extern/pcsx2`.
- `meson.build`, `meson_options.txt` - one build for both flavors (`minibox_dir`, `mesa_guest_dir`, `gl_bridge`, `jit_rasterizer`).
- `waterbox/sources.sh` - the curated list of upstream sources both flavors compile.
- `waterbox/cinterface.cpp`, `host.cpp`, `host-sys.cpp`, `gs-device.cpp`, `audio-stream.cpp`, `stubs/` - the adapter between PCSX2 and the guest ABI.
- `waterbox/arcade/` - the NAMCO board (JVS, ATA/ATAPI, SRAM), kept beside upstream.
- `waterbox/game-database.cpp`, `gen-gamedb.py`, `gen-shaders.py` - PCSX2's per-title database and shaders, compiled into the core.
- `waterbox/gl-osmesa.cpp`, `gl-bridged.cpp`, `gl-host.c`, `gl-entry-points.txt` - OpenGL in the sandbox, and the GPU bridge's guest and host halves.
- `waterbox/run-native.c`, `run-wbx.c`, `gate-harness.h` - the native reference and the sandbox driver.
- `waterbox/apply-patches.sh`, `setup-mesa.sh`, `setup-guest.sh`, `build-package.sh`, `run-gate.sh` - the build and the core gate.
- `waterbox/waterbox.config`, `file_slots.json`, `default_keybinds.json`, `package-licenses.json` - what the package declares to Chimera.
- `waterbox/tests/` - the frontend gate (`run-frontend.sh`) and its helpers.
- `tests/own/` - redistributable test content (tracked). `tests/roms/` - your own bios and discs, ignored wholesale.
- `docs/PLAN.md` - the design log: decisions, measurements, sharp edges.
- `build/` - every build output. Ignored by git.

## Set up the build environment

Ubuntu, as CI uses. Set the two paths first; both must be absolute.

```sh
chimera="$HOME/chimera"                        # a Chimera checkout
mb="$chimera/extern/chimera-common-minibox"    # miniBox, a submodule of it

sudo apt-get update
sudo apt-get install -y --no-install-recommends meson ninja-build build-essential python3 bison flex pkg-config python3-mako python3-packaging

git submodule update --init

[ -d "$chimera" ] || git clone https://github.com/ToolAssisted-run/chimera.git "$chimera"
git -C "$chimera" submodule update --init extern/chimera-common-minibox

[ -f "$mb/build/meson-linux/build.ninja" ] || meson setup "$mb/build/meson-linux" "$mb"
meson compile -C "$mb/build/meson-linux"
[ -f "$mb/build/meson-cpp/build.ninja" ] || meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true
meson compile -C "$mb/build/meson-cpp"
```

## Build

Shortest path to a package (what the workflow's `frontend-gate` job runs):

```sh
bash waterbox/setup-mesa.sh -m "$mb"     # once: fetches Mesa 24.0.9 (SHA-256 pinned), builds build/mesa
./waterbox/build-package.sh -m "$mb" -r "$chimera"
```

For the core gate you also need the native reference, and the guest built by
hand (what the `core-gate` job runs):

```sh
meson setup build/meson-native -Dminibox_dir="$mb"
ninja -C build/meson-native
MINIBOX_DIR="$mb" sh waterbox/setup-guest.sh -- -Dminibox_dir="$mb" -Dmesa_guest_dir="$PWD/build/mesa"
ninja -C build/meson-guest
```

- `setup-mesa.sh` needs `bash` and network access the first time.
  `MESA_BUILD_CONFIGURE_ONLY=1` checks the recipe without compiling.
- The patches are applied by `meson.build` at configure time. Nothing to run.
- `build-package.sh` configures `build/meson-guest` only if it has no
  `build.ninja`. An existing directory keeps the options it was configured
  with, including a missing `-Dmesa_guest_dir`.
- A hand-built package is stamped `<commit>+local` (`-dirty` with changes in
  the tree, which includes the applied patches). CI stamps the commit.

## Install the core into Chimera

`build-package.sh -r "$chimera"` writes
`$chimera/build/Cores/pcsx2.chimeraCore`: the cores folder of a Chimera
source checkout, so nothing else is needed there. For a release bundle, copy
the file into the `Cores` folder beside `Chimera.exe` (or the folder set in
File > Core Manager > Change folder...). File > Core Manager lists the folder;
Refresh List rescans it. Chimera downloads nothing. The same file runs on
Linux and on Windows.

## Test before you commit

```sh
./waterbox/run-gate.sh                                        # the core gate
./waterbox/tests/run-frontend.sh --chimera-root "$chimera"    # the frontend gate
```

- **Know what ran.** Both gates must end with `0 failed`. A PS2 has no HLE
  bios, so with nothing provided the core gate runs five checks (`keybinds`,
  `arcade:panel-words`, `nobios:refuses`, `gamedb:loaded`, `gamedb:entry`)
  and the frontend gate runs none. That is all CI proves about the machine.
- To gate the machine, provide a bios: the first `*.bin` in
  `tests/roms/bios/`, or `CHIMERA_PS2_BIOS=<file>`. A disc image (`*.bin` or
  `*.iso` in `tests/roms/`) adds the disc checks and lets the frontend gate
  run. If you have no bios, say in your report that the machine checks did
  not run; do not present the green tier as proof.
- The GPU checks (`gpu:bridge`, `gpu:picture`) need `run-wbx` built with
  `-Dgl_bridge=true` and a GL context. `ports:columns` and
  `gl:rebuild-at-zero` need `$CHIMERA_ROOT` with
  `build/meson-linux/chimera-run` and the installed package. The arcade
  checks need `PCSX2_S246_ROMS`, `PCSX2_S256_ROMS` or `PCSX2_SS256_ROMS`.
- The frontend gate needs Chimera built (natives and the .NET solution), the
  package installed, `run-native`, Mono and Xvfb. See `docs/BUILDING.md`.
- A check that was skipped has proven nothing about your change.

## Rules of this repository

- **Upstream is patched, not edited.** `extern/pcsx2` stays at its pin.
  Changes to it are numbered patches in `patches/`
  (`NNNN-chimera-<what it does>.patch`, `git diff` format, paths relative to
  the submodule). `waterbox/apply-patches.sh` applies the whole series or
  none, in file-name order. `git status` shows `extern/pcsx2` as modified
  once it is applied; that is expected. Never commit inside the submodule.
  After adding or changing a patch, `waterbox/apply-patches.sh` must print
  `already applied: all N patches`.
- **Determinism is the product.** The guest must not read host time, host
  randomness or anything else that differs between runs, and a savestate
  must round-trip. The gate checks both when it has a bios; a change that
  breaks either is a bug. Only the `opengl-hw` renderer is declared
  non-deterministic (`waterbox.config`): its picture comes from the host's
  GPU.
- **Run the gate before committing.** A new check needs a negative control:
  show that it fails when the thing it checks is broken.
- **Never commit game files, BIOS or firmware.** `tests/roms/` is ignored
  wholesale and is where a local bios and discs go. Only content that is
  free to distribute is tracked, in `tests/own/`, with its source and terms
  in `tests/own/README.md`.
- **Never add network access** to the core. A sandbox has no sockets.
- **Scripts stay executable.** Every `.sh` under `waterbox/` is git mode
  100755; meson and CI run them directly.
- **Documentation prose is plain ASCII.**
- **Commit messages** follow the log: `type(scope): a sentence that says what
  is now true`, for example
  `fix(dongle): the boot software could not read its own card`. The body is
  prose: the cause, the fix, what was measured, and the gate count. Issues
  live in the chimera repository and are cited as `chimera#N`. Assisted
  commits end with a `Co-Authored-By:` trailer.
- **Do not edit `.github/workflows`** unless the task is the workflow.

## Where to read more

- `docs/BUILDING.md` - every build step, option and gate check.
- `docs/PLAN.md` - why each decision was made; grep it before changing a patch.
- `.github/workflows/chimera.yml` - the recipe CI runs.
- `tests/own/README.md`, `LICENSE`, `waterbox/package-licenses.json` - terms of the test content and the package.
- In the Chimera checkout: `docs/porting-a-core.md` (how a core is put together, patch traps), `docs/gates.md` (how a green gate can be wrong), `docs/core-manager.md` (packages, versions, the cores folder).
