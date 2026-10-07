# chimera-core-pcsx2

PCSX2 - the PlayStation 2 - as a Chimera waterbox core.

Upstream is `PCSX2/pcsx2`, pinned as a submodule at `extern/pcsx2`.

Two things make this port different from every other core in the bundle. It
comes with a real SOFTWARE renderer, maintained upstream for accuracy, so the
picture is not the problem it was for Flycast. And it cannot run ANYTHING
without a bios - a PS2 has no HLE bios - so what its gate can prove depends on
content the user supplies locally (never committed; see .gitignore).

The CPUs are a setting (`cpu_core`): PCSX2's recompilers for the EE, the IOP
and both vector units, which is the default, or its interpreters. They are
different machines - the recompiler's EE floating point is not the
interpreter's - so a project pins the choice. The software rasteriser takes its
C++ scanline path either way.

## Using it in Chimera

Chimera ships no cores and downloads nothing. Download the `.chimeraCore`
package from this repository's
[Releases](https://github.com/ToolAssisted-run/chimera-core-pcsx2/releases)
page, or build it, and put it in the `Cores` folder beside `Chimera.exe`;
File > Core Manager lists what is there. The same file runs on Linux and on
Windows. The bios and the games are yours to provide.

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
- **with a bios** in `tests/roms/bios/` (or `$CHIMERA_PS2_BIOS`): equivalence,
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
