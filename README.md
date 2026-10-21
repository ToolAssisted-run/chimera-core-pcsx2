# chimera-core-pcsx2

PCSX2 - the PlayStation 2 - as a Chimera waterbox core.

Upstream is `PCSX2/pcsx2`, pinned as a submodule at `extern/pcsx2`.

Two things make this port different from every other core in the bundle. It
comes with a real SOFTWARE renderer, maintained upstream for accuracy, so the
picture is not the problem it was for Flycast. And it cannot run ANYTHING
without a bios - a PS2 has no HLE bios. Besides the console's own, the package
accepts OpenPS2BIOS, an open replacement, which the tests and CI run on.

The CPUs are a setting (`cpu_core`): PCSX2's recompilers for the EE, the IOP
and both vector units, which is the default, or its interpreters. They are
different machines - the recompiler's EE floating point is not the
interpreter's - so a project pins the choice. The software rasteriser takes its
C++ scanline path either way.

PCSX2's game database is compiled into the core, its compatibility patches
included: a game gets the corrections PCSX2 gives it, and a package pins them.
Cheats and the patches a person chooses (widescreen, no-interlacing) are not
applied.

It is built for x86-64 and for aarch64 (on the aarch64 forks of Chimera and
miniBox), one package per CPU. PCSX2 has no recompilers for arm64, so the
aarch64 package has its interpreters alone.

## Using it in Chimera

Chimera ships no cores and downloads nothing. Download the core's package
from this repository's
[Releases](https://github.com/ToolAssisted-run/chimera-core-pcsx2/releases)
page, or build it, and put it in the `Cores` folder beside `Chimera.exe`;
File > Core Manager lists what is there.

Each release has two packages, one per CPU: `pcsx2-<version>-x64.chimeraCore`
and `pcsx2-<version>-arm64.chimeraCore`. **You most likely want the x64 one.**
Take the arm64 one only if you know you are using an arm64 device. Put only one
of them in the `Cores` folder. The same file runs on Linux and on Windows. The
games are yours to provide, and so is the bios: a copy of your own console's,
or OpenPS2BIOS 0.2
([download](https://github.com/TamaribaEmu/OpenPS2BIOS/releases/tag/v0.2),
Boot ROM setting "OpenPS2BIOS 0.2"), which is free but does less, so a game
may not run on it.

## Building

The build, in short - it needs a Chimera checkout with miniBox built, and
[`docs/BUILDING.md`](docs/BUILDING.md) has every step, option and requirement.
[`AGENTS.md`](AGENTS.md) is the operating guide for an AI coding agent.

```sh
bash waterbox/setup-mesa.sh               # the guest Mesa: fetched once (SHA-256 pinned), built into build/mesa
meson setup build/meson-native            # the native reference and the runners
ninja -C build/meson-native
sh waterbox/setup-guest.sh -- -Dmesa_guest_dir="$PWD/build/mesa"   # the sandboxed core
ninja -C build/meson-guest
```

## The gate

```sh
./waterbox/run-gate.sh
```

The sandboxed core must produce byte-identical video, audio, lag and
memory-domain digests to the same sources built natively, and must survive a
whole-machine savestate round-trip around every frame.

It runs in tiers, because a PS2 needs a bios to do anything at all:

- **with nothing**: both flavors must refuse a machine with no bios, and say
  why. That is the path every user without a dump meets first.
- **with a bios** in `tests/roms/bios/` (or `$CHIMERA_PS2_BIOS`, else
  OpenPS2BIOS built from `extern/openps2bios`): equivalence,
  the savestate round-trip, determinism, the memory domains, the picture, the
  pad, lag counting, and the save-data channel carrying the memory cards and
  the console's NVRAM.
- **with a disc** in `tests/roms/`: the game's own program loading, and running
  identically in the sandbox.

Checks whose content is missing report SKIP and say what they would have
proven, so a green run never quietly means nothing was tested.

## The package

```sh
./waterbox/build-package.sh -r <chimera checkout>   # -> build/Cores/pcsx2.chimeraCore
./waterbox/tests/run-frontend.sh                    # the package inside Chimera
```

The frontend gate loads the package in Chimera itself (headless, under Mono)
and requires the machine it builds to be the machine the core gate signed off
on, that a setting arrives through the frontend, and that the package's
bindings become the frontend's defaults.

The port's history, the patches and what remains are in
[`docs/PLAN.md`](docs/PLAN.md).

Status: **M1 to M6.** A commercial game boots, draws, takes input and saves,
the sandbox is byte-identical to the native reference throughout, and the
package runs inside Chimera.
