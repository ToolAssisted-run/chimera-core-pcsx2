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

## What the first boot answered

**Speed: about 20 frames per second at the bios, on this machine.** 600 frames
of the PS2's own boot animation took 29 seconds - EE, IOP and both vector units
interpreting, the software renderer on its C++ scanline path, no fastmem, no
recompilers, everything single-threaded. That is a THIRD of real time for a
light scene, not the seconds-per-frame this port was braced for, and it settles
the question the plan was gated on: the shape is worth finishing. A real game
does far more work per frame than a boot logo does, so the recompilers remain
the open question for playable speed - but they are now an optimisation rather
than a precondition.

## Milestones

- **M1 - the machine builds and initialises.** DONE 2026-08-27. Curated source list, meson for
  guest and native reference, a Host implementation, `cinterface.cpp` against
  the Chimera guest ABI. Proof (bios permitting): the EE executes to a fixed
  frame count identically native and waterboxed.
- **M2 - no threads, no fastmem.** DONE 2026-08-27 (patch 0004 and the
  interpreter settings). The gate leg that proves it is M4's work.
- **M3 - the picture.** DONE 2026-08-27: the software renderer draws, a
  graphics device made of memory (waterbox/gs-device.cpp) merges the two
  display circuits, and the frame leaves through GetVideoBgra. The bios
  animation is visible at 640x448. Textures and interlacing are undrawn by
  anything so far; a real disc is what will test them.
- **M4 - input, domains, savestates.** The legs every other core has.
- **M5 - discs and memory cards.** ISO/CHD through the file slots, the memory
  card through the save-data channel, the bios through the firmware channel.
- **M6 - the frontend leg.** The package inside Chimera.

## Log

- **2026-08-27** Feasibility settled (this document). Repo created, upstream
  pinned at `e1dd0a0`. The verdict: harder than Flycast in two places (no HLE
  bios, a mandatory GS thread), easier in one that matters more (a real
  software renderer already exists), and gated on content the user will supply.

- **2026-08-27** M1, M2 and M3 in one pass, driven by the user's bios dumps.
  What it took, and what is worth remembering:

  - **225 curated sources plus 108 dependency sources.** The recompilers and
    the disassembler are COMPILED but never called: the EE and IOP opcode
    tables name both implementations of every instruction, so a build without
    them is a build with five hundred undefined symbols. Compiling what you do
    not call was far cheaper than stubbing it, and it leaves the recompilers
    one setting away.
  - **zlib, zstd and lz4 are now submodules.** PCSX2 takes all three from the
    system; a sandbox has no system. They are what CHD, CSO and the GS dump
    formats are made of.
  - **Four patches**, each a build option rather than a deletion: the scanline
    JIT (0001), the hardware renderer and its device factory (0002), the SDL
    input source under the pad container (0003), and the GS thread (0004).
  - **The GS runs inline.** MTGS is a ring buffer with one producer and one
    consumer; patch 0004 lifts the consumer out of the thread's main loop into
    a function, and the producer calls it. Five entry points changed; the ring
    buffer, the packet formats and the GS itself are untouched.
  - **The infinite loop that cost the most.** With no GPU backends compiled,
    `GetAPIForRenderer` falls through to "ask for the PREFERRED renderer",
    which in this build is the software one, which falls through again. The
    open hung, silently, with the log buffered where nobody could see it.
  - **Logging has to be set through PCSX2's own settings.** Calling
    `Log::SetConsoleOutputLevel` directly works right up until the next
    settings load turns it back off, which is where the silence came from.

## The sandbox

**2026-08-27, the same day: the PS2 runs inside the waterbox, and it is the
same machine as the native one.** 600 frames of the bios, byte for byte, on
every channel - the picture, the audio, EE RAM, IOP RAM, the scratchpad and
both vector units - and the whole guest survives a save/load round-trip on
every single frame. The sandbox costs about 12 percent: 300 frames in 18.5
seconds against 16.4 native.

What it took, in the order the sandbox found them:

- **No shared memory** (patch 0005). PCSX2 allocates the PS2's memory as shared
  memory so it can be mapped twice, which is what fastmem needs and nothing
  else does here. Both regions are now plain mappings - and the CODE region is
  mapped EXECUTABLE, which is worth knowing: miniBox can host executable
  memory, so the recompilers are not architecturally excluded.
- **Fastmem honours its own switch** (patch 0006). The 4GB reservation was
  unconditional; PCSX2 already had the setting, upstream just never consulted
  it here.
- **The GS's video memory** (in patch 0002) is mapped four times in a row so
  that address arithmetic wraps. Four real copies instead - the difference is
  visible only to a program that reads past the end of video memory.
- **A flat, read-only file system** (patch 0007). A sandbox has one directory
  and no writes. The second half of that is what fixed the ONLY divergence
  this port has had: the native reference was creating memory card files in
  the work directory and the sandboxed build could not, so the two machines
  saw different hardware and split at frame 60 - the exact frame the bios first
  polls a memory card. Refusing writes in both flavors made them the same
  machine again. (Memory cards therefore have no home yet; that is M5.)
- **The VIF unpack recompiler** (patch 0008) generates code whichever cpu
  implementation is selected. It has an interpreted twin, now reachable.
- **A clock the machine owns.** The first sandbox clock advanced one tick per
  READ, which is not deterministic at all: a build that reads it more often
  sees a different time. It now advances one frame's worth of microseconds per
  frame, and reading it does nothing.
- **Three syscalls** the sandbox does not have: mkdir, prctl, and the two glibc
  extensions musl lacks (gettid and two sysconf queries).

## What is known to be wrong, and is next

- **The equivalence gate is not written yet**, though everything it needs now
  exists and passes by hand: 600 frames, both flavors, plus the state
  round-trip.
- **Memory cards have no home.** They are switched off, because a card is a
  file and this core writes no files. The save-data channel is M5.
- **Discs are untested.** Only the bios has ever been booted; ISO, CHD and CSO
  all compile and none has been read.
- **Lag detection reports every frame as a lag frame**, because nothing calls
  `chimera_input_was_read` yet - the hook belongs where the pad answers the
  SIO's poll.
