# chimera-core-pcsx2

PCSX2 - the PlayStation 2 - as a Chimera waterbox core.

Upstream is `PCSX2/pcsx2`, pinned as a submodule at `extern/pcsx2`.

Two things make this port different from every other core in the bundle. It
comes with a real SOFTWARE renderer, maintained upstream for accuracy, so the
picture is not the problem it was for Flycast. And it cannot run ANYTHING
without a bios - a PS2 has no HLE bios - so what its gate can prove depends on
content the user supplies locally (never committed; see .gitignore).

Everything is interpreted: the EE, the IOP, both vector units, and the software
rasteriser's C++ scanline path. A game runs at about twenty frames a second,
and the sandbox costs almost nothing on top of that. The recompilers are an
optimisation this core has not needed yet, and the sandbox turns out to be able
to host executable memory, so they are not ruled out.

## Building

```sh
meson setup build/meson-native            # the native reference and the runners
ninja -C build/meson-native
sh waterbox/setup-guest.sh                # the sandboxed core
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

The port's history, the ten patches and what remains are in
[`docs/PLAN.md`](docs/PLAN.md).

Status: **M1 to M5.** A commercial game boots, draws, takes input and saves,
and the sandbox is byte-identical to the native reference throughout.
