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
- **M5 - discs and memory cards.** DONE 2026-08-27 for what a project needs:
  the disc through a file slot, the bios through the firmware channel, the
  memory cards and the console's NVRAM through the save-data channel. CHD and
  CSO compile but have not been read.
- **M6 - the frontend leg.** DONE 2026-08-27. The package
  (`waterbox.config`, the file slots, the DualShock 2's default bindings, the
  licences and the deterministic zip) loads in Chimera, and 200 frames of a
  game inside the frontend are byte-identical to the native reference.

## The package

`waterbox/build-package.sh` builds `pcsx2.chimeraCore`: the core, the config,
the file-slot declaration the wizard renders, the DualShock 2's default
bindings, and the licences of all thirteen components. Deterministic - the
zip's sha1 is checked twice at build time, because that hash is the core's
identity and movies cite it.

Two things the frontend leg found that nothing else could have:

- **A rom opened directly is not a project.** The core only knew how to find a
  disc through a project's slot map; a rom opened from the command line arrives
  mounted under the name `waterbox.config` calls `romFile`, and the core booted
  an empty tray. The first version of the gate leg did not notice, because two
  hundred frames of the console's own startup look the same with an empty tray
  as with a disc - so that leg now boots the disc's own program instead.
- **A setting has to be tested where it lands.** The console's clock changes
  the IOP's memory long before the EE asks what time it is.

## The gate

`waterbox/run-gate.sh`, in tiers, because a PS2 needs a bios to do anything:
with nothing it proves both flavors refuse a machine with no bios and say why;
with a bios it proves the machine (equivalence, the savestate round-trip,
determinism, the domains, the picture, the pad, lag counting, save data); with
a disc it proves a game loads and runs identically in the sandbox. Missing
content reports SKIP with what it would have proven. 13 of 13 green locally.

Lag detection landed with it (patch 0011): a lag frame is a frame the machine
never looked at its input, and a PS2 looks where the pad answers the SIO poll.
The first 29 frames of a cold boot are lag frames - the IOP has not loaded its
pad driver - and the count stops growing the moment it has.

## Log

- **2026-08-30** Eight controller slots and eight memory cards. `port1`..`port8`
  choose a device, `multitap1`/`multitap2` plug in the taps that make slots 3-8
  reachable at all, and `memcard1`..`memcard8` say which sockets hold a card.
  The two PHYSICAL ports also take the instruments - a guitar, a Jogcon, a
  Negcon, a Pop'n controller - and carry the extra controls those need; a
  multitap slot takes a DualShock 2 and nothing else, because every declared
  control costs a column in every movie recorded with this core and nobody
  builds that machine. THE MOVIE FORMAT CHANGED: every column is now `P1 Cross`
  rather than `Cross`, and there are 176 of them.

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

## A game

**2026-08-27: Golden Axe (SLPM-62385, a raw 2352-byte-sector PS2 CD) boots to
its character select screen** - a textured 3D model, an alpha-blended
background, Japanese text - and the sandboxed build is byte identical to the
native one over 1600 frames including scripted input.

- **Speed with a real game: about 19-25 fps** (1600 frames in 64s, 4000 frames
  in 211s), and the sandbox costs nothing measurable here: 63.4s against 64.2s
  over the same 1600 frames.
- **The disc needed nothing.** PCSX2 read the raw CD image as it was, found the
  PVD, parsed SYSTEM.CNF and loaded the ELF. Fast boot (a project setting)
  skips the bios animation and boots it directly.
- **Input reaches the machine**, through PCSX2's own pad container: the game's
  "no memory card, start anyway?" prompt was answered with a scripted LEFT and
  CIRCLE, and it moved on.
- **Deinterlacing had to be pinned off.** PCSX2's default is Automatic, which
  chose a MOTION ADAPTIVE deinterlacer - it compares the last three fields and
  decides per pixel what to show. That is not a picture a movie can promise to
  reproduce, and it arrived as two half-height copies of the frame stacked on
  top of each other. Progressive is the merged frame the GS actually produced.
- Two more syscalls the sandbox lacked: getcwd (the answer is "here") and
  lstat (there are no links).
- **A slot's block is not a multiplication.** Eight slots, but the two physical
  ports declare twenty controls the other six do not, so `slot * 17` is wrong
  for everything past the second and quietly reads somebody else's buttons.
  Every index goes through `SlotButtonBase()`.
- **The gate harness has its own bound.** `--press 400:100:45` did nothing at
  all and reported no error: `GATE_BTN_COUNT` was still 17, so the press was
  dropped before it reached the core, and player 2 looked dead when it was
  fine. A harness that silently ignores an out-of-range control is a harness
  that can only ever confirm what it already believed.

## Memory cards

**2026-08-27: the cards travel through the save-data channel** (patch 0009 and
the ABI's save-data exports), and the whole path is proven end to end:

- The PS2's own browser sees a card in slot 1, calls it Unformatted, formats it
  on request ("Formatting completed. 7,998 KB Free"), and the bytes that come
  out of the save-data channel begin with "Sony PS2 Memory Card Format 1.2.".
- Mounted back on the next run, the same card reports **Formatted**.
- Golden Axe, which opens by warning that it cannot save, no longer warns: with
  the card present it goes straight to its opening cutscene.
- The card the SANDBOX produced is byte identical to the one the native
  reference produced, over the same 2800 frames of browser navigation.

The patch is small because it does not reimplement anything. PCSX2's file card
is good code - ECC, checksums, erase blocks, the lot - and all of it is written
against a `FILE*`. So the card is a buffer opened as a stdio stream
(`fmemopen`, unbuffered so that a write lands in the buffer the moment the
machine makes it), and every read, write, erase and checksum in that file goes
on working exactly as written, into memory instead of onto a disk.

## What the console remembers

The NVRAM travels the same way (patch 0010). It holds the language, the clock
configuration, the region parameters and the machine's iLink id, and without it
a PS2 asks for a language on every cold boot - which it did here, every run.
It was already a buffer (`s_nvram`, one kilobyte); upstream only touched a file
to remember it between runs, so the patch is an accessor and a write that no
longer happens.

With the saved NVRAM mounted back, the console skips its setup screens and goes
straight to the browser. Guest and native produce the same kilobyte.

## What is known to be wrong, and is next

- **Only one disc format is tested**: a raw 2352-byte-sector CD. CHD and CSO
  compile and neither has been read.
- **Only ISO/BIN discs are tested.** CHD, CSO, ZSO and GZ compile, and the
  package declares them, but none has been read.
- **The instruments are declared, and barely exercised.** A physical port can
  be set to `guitar`, `jogcon`, `negcon` or `popn`; each is built and the
  machine reports it, and the wire from a frontend column to the pad's own
  input index is written down. What nothing here does is play a game with one -
  whether a whammy bar FEELS right is a question only Guitar Hero can answer.
- **The pressure modifier is not offered.** A DualShock 2's buttons are
  pressure-sensitive and `PAD_PRESSURE` scales how hard the host is pressing
  them. It is a host convenience rather than a control the machine has, so it
  is left out; a movie that wants half-pressed buttons has no way to ask yet.
