# PCSX2 as a Chimera core: plan and log

## What this is

A PlayStation 2 for Chimera, built from upstream `PCSX2/pcsx2` (submodule,
pinned) in miniBox's sandbox.

It is the second core here with no BizHawk precedent, and the first that cannot
run anything at all without content the user supplies: a PS2 has no HLE bios,
so nothing boots until a real one is present. That shapes the whole schedule -
the work can be done, but the GATES cannot close until a bios exists on this
machine.

## The five questions, answered before writing code

**1. How does it draw a frame without a GPU?** Better than any core so far.
PCSX2 ships a maintained SOFTWARE renderer (`pcsx2/GS/Renderers/SW`), used for
accuracy rather than as an afterthought - this is the opposite of Flycast,
where a rasteriser had to be found elsewhere and ported.

One detail matters: that renderer JIT-compiles its scanline routines
(`GSDrawScanlineCodeGenerator`, via xbyak). A sandbox would rather not host a
code generator, and it does not have to: there is a pure C++ path,
`GSDrawScanline::CDrawScanline`, selected when `ENABLE_JIT_RASTERIZER` is off.
Slower, and correct.

**2. Can it build headless?** Yes, and there is a worked example in-tree:
`pcsx2-gsrunner` is a headless application that drives the core to replay GS
dumps, so the Host interface a frontend must implement (about 55 functions in
`pcsx2/Host.h`) has a reference implementation to read. `ENABLE_QT_UI=OFF`
drops the GUI. The core is roughly 365 translation units once the GUI, the
GPU backends and the platform layers are excluded - between Stella and PPSSPP,
and tractable.

**3. Does it need a real address space?** This is the hard one. `vtlb_Core_Alloc`
reserves a **4GB fastmem area** unconditionally and fails if it cannot, which a
waterbox guest cannot possibly satisfy. Fastmem exists for the RECOMPILERS;
the interpreters go through the vtlb's own page map, and `EmuConfig.Cpu.
Recompiler.EnableFastmem` already exists as a switch. So the patch is to honour
that switch in the allocator - small, and in the same spirit as Flycast's
refusal to reserve the SH4's address space.

The rest of the map is modest: EE memory, IOP memory, VU memory and the two
vtlb tables come to about 60MB, which is an ordinary guest heap.

**4. Threads?** The worst answer of any core so far. The GS runs on its OWN
THREAD, unconditionally: `MTGS::Open` starts one and there is no
single-threaded mode left in the codebase. A sandbox has no threads at all.
What makes this survivable is that MTGS is a ring buffer with one producer and
one consumer, so the consumer can be pumped INLINE - the EE submits a packet
and the GS processes it there and then. That is a real patch against a real
subsystem rather than a hook, and it is the largest single piece of work in
this port.

**5. Does it need a bios?** Yes, and there is no way around it. PCSX2 has no
HLE bios; `VMManager` fails if `LoadBIOS()` does. Every other core here could
be gated on content this repository builds itself; this one cannot. The gates
are therefore written to SKIP with a clear message when no bios is present, and
what they prove waits for the user's own dump (see the local-content paths in
.gitignore).

## What is uncertain, and will decide whether this is worth finishing

- **Speed.** A PS2 interpreted (EE, IOP and both vector units), with a software
  GS whose scanline JIT is disabled, inside a sandbox. Every one of those
  choices costs an order of magnitude. It may be seconds per frame. Nothing is
  known until a bios exists and the first frame runs, and that number decides
  whether the recompilers become the next milestone or the project stops.
- **Determinism.** Untested, and the surface is large: the EE and IOP run on a
  shared clock, MTVU is a second thread when enabled (it will not be), and the
  GS has its own timing. The equivalence gate is the instrument, as always.
- **Savestates.** PCSX2 has them and they are thorough; whether they survive
  per-frame round-tripping under the sandbox is what the gate will say.

## Milestones

- **M1 - the machine builds and initialises.** Curated source list, meson for
  guest and native reference, a Host implementation, `cinterface.cpp` against
  the Chimera guest ABI. Proof (bios permitting): the EE executes to a fixed
  frame count identically native and waterboxed.
- **M2 - no threads, no fastmem.** The two structural patches, each with the
  gate leg that shows the machine still runs.
- **M3 - the picture.** The software renderer, through the C scanline path,
  into the frontend's framebuffer.
- **M4 - input, domains, savestates.** The legs every other core has.
- **M5 - discs and memory cards.** ISO/CHD through the file slots, the memory
  card through the save-data channel, the bios through the firmware channel.
- **M6 - the frontend leg.** The package inside Chimera.

## Log

- **2026-08-27** Feasibility settled (this document). Repo created, upstream
  pinned at `e1dd0a0`. The verdict: harder than Flycast in two places (no HLE
  bios, a mandatory GS thread), easier in one that matters more (a real
  software renderer already exists), and gated on content the user will supply.
