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
- **M7 - the light gun.** DONE 2026-09-19. The GunCon 2, on the console's own
  USB bus, aimed from the movie's axes and never from a mouse. What it cost
  and what it could not prove are in the log.
- **M8 - the NAMCO arcade boards.** DONE 2026-09-20 for System 246, on a game
  (chimera issue #72). System 246, System 256 and Super System 256 as three
  more machines in this package rather than a core of their own - the way
  Flycast carries NAOMI, NAOMI 2 and Atomiswave. What was proven and what was
  not is in the log.

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

The NAMCO boards have six legs of their own, once per board, behind
`PCSX2_S246_ROMS`, `PCSX2_S256_ROMS` and `PCSX2_SS256_ROMS`: equivalence, the
machine ran, the savestate round-trip, the panel (a coin, which is the one
control every cabinet has and the one thing a board in attract mode cannot
ignore), the picture, and that the run wrote nothing into the project. They
carry their own bios, so they run whether or not a console one is present, and
they SKIP when no folder is named - which is what CI does.

Lag detection landed with it (patch 0011): a lag frame is a frame the machine
never looked at its input, and a PS2 looks where the pad answers the SIO poll.
The first 29 frames of a cold boot are lag frames - the IOP has not loaded its
pad driver - and the count stops growing the moment it has.

## Log

- **2026-09-21** Nine graphics options, classified by measurement (chimera
  issue #122): two declared, seven not, and the reason for each is a number.

  The issue asked for PCSX2's Aspect Ratio, FMV Aspect Ratio Override,
  Deinterlacing, Bilinear Filtering, Anti-Blur, Texture Filtering, Mipmapping,
  Auto-Flush and FXAA. Sergio's rule: a post-processing option (one that
  cannot change a byte of the machine) belongs to the frontend's display
  settings; an option internal to the core becomes a core setting, applied by
  the core's own implementation. The sorting was done by running each option
  at two values and comparing every memory domain, the audio, the lag count
  and the whole-run picture hash. Every option was wired to PCSX2's own key
  for the measurement (`AspectRatio`, `FMVAspectRatioSwitch`,
  `linear_present_mode`, `pcrtc_antiblur`, `fxaa`, `filter`, `hw_mipmap`,
  `UserHacks` + `UserHacks_AutoFlushLevel`); the instrument was run against
  itself first (two identical runs, every flavour, every disc: byte-equal).

  Software renderer, native, Maximo, 300 frames (five domains, audio, lag,
  picture): nothing changed anything except `deinterlace=off` (picture only,
  as it always has) and `fxaa=true`, which produced a BLACK frame - the
  headless device has no shader stage, `DoFXAA` is a no-op, and `GSDevice::
  FXAA` then presents the untouched output texture. That is why fxaa is
  forced off unless the GL device is up. `pcrtc_antiblur=false` was then
  run natively over 900 frames of all five discs on hand (Gran Turismo 4,
  Street Fighter EX3, Time Crisis II, Marvel vs. Capcom 2, Maximo): identical
  in every line.

  GPU bridge (`opengl-hw`, llvmpipe on this box), sandbox:

  | disc | frames | filter=nearest | hw_mipmap=off | autoflush=all | antiblur=off | fxaa=on |
  | --- | --- | --- | --- | --- | --- | --- |
  | Maximo | 300 | = | = | = | = | picture |
  | Street Fighter EX3 | 600 | = | = | = | = | picture |
  | Gran Turismo 4 | 900 | picture | = | = | = | picture |
  | Time Crisis II | 2400 | = | = | = | = | - |
  | Maximo | 2400 | picture | = | = | = | picture |
  | Gran Turismo 4, Start pressed at 400/1500/2500 | 3000 | picture | = | = | = | - |
  | Maximo | 6000 | picture | = | = | = | - |

  "picture" means the whole-run video hash and the last frame's differ and
  EE RAM, IOP RAM, the scratchpad, VU0, VU1, the audio and the lag count are
  byte-identical; "=" means every line identical, picture included.
  Aspect ratio (16:9, stretch), the FMV switch (16:9) and bilinear
  presentation (smooth, sharp) were identical under both renderers too.

  What that decided:

  - **Aspect Ratio, FMV Aspect Ratio Override, Bilinear Filtering: inert in
    this core, by construction and by measurement.** All three act in
    PCSX2's present pass (`GSRenderer::PresentCurrentFrame`,
    `GetCurrentAspectRatioFloat`, `LinearPresent`), and this core does not
    present: `ChimeraGSGetFrame` hands over the merged texture and the
    frontend draws it. The frontend's Display configuration already has an
    aspect-ratio selection (system, custom size, custom ratio, 1:1) and a
    final filter (none, bilinear), which is where these two belong under the
    rule and where they already are. The FMV switch has no frontend
    equivalent - nothing outside the core knows when an FMV plays - and is
    not offered.
  - **Deinterlacing: already declared (issue #7), and now measured to be
    picture-only under both renderers.** Unchanged.
  - **Texture Filtering: declared, `textureFiltering` (machine / nearest /
    linear / linearNoSprites).** Changes the picture on Maximo and Gran
    Turismo 4, never a byte of memory in the frames run. It is not
    post-processing all the same: it decides what lands in every render
    target, and `GSTextureCache::Read` writes a render target back into
    `GSLocalMemory` when a game reads its own picture, so on such a game the
    bytes read depend on it. The declaration says both, and that a movie
    needs the same value to play back.
  - **FXAA: declared, `fxaa` (bool), GL renderers only.** Applied to the
    merged picture after the PCRTC; nothing reads that texture back, so it
    is post-processing in the strict sense and cannot desync. It is a core
    setting rather than a frontend filter because it is PCSX2's own shader
    and the frontend has no shader stage to run one in (its display filter
    chain is a letterbox and a bilinear switch), and because a value reaches
    a core only as a declared setting. Its description says a movie made
    with it plays back without it.
  - **Mipmapping, Auto-Flush, Anti-Blur: not declared.** Not one pixel
    changed on any disc up to 6000 frames. A setting nobody can see change
    anything is one no leg could hold red (docs/gates.md, B), and a setting
    with no leg is a promise with nothing behind it. They stay at PCSX2's
    defaults (hardware mipmapping ON, auto-flush OFF, anti-blur ON). When a
    disc turns up on which one of them moves the picture, it is two lines
    in cinterface.cpp and a column in the `gpu:picture` leg.

  The leg: `gpu:picture` runs `PCSX2_GFX_DISC` on the bridge at the default,
  at `textureFiltering=nearest` and at `fxaa=true` (`PCSX2_GFX_FRAMES`, 2400,
  where Maximo first shows the filter) and requires the machine identical and
  three different pictures. SKIPs without the disc (CI). Negative control:
  with the two assignments removed from cinterface.cpp, all three runs drew
  the baseline hash. What it does not stand in for: a game that reads its
  picture back, where the filter is the machine.

  Also from this round: `MAX_WIDTH`/`MAX_HEIGHT` still crop at 1280x1024, so
  an internal-resolution setting for this core is still the decision recorded
  in chimera's docs/graphics-settings.md, not a patch.

- **2026-09-21** The gate's own GL host had no case for the context id, and
  the gate had never run that host at all.

  Found on rpcs3 (its b1b88fe) and checked here the same morning.
  `waterbox/gl-host.c` is the host half of the GPU bridge that `run-wbx`
  hands `core.wbx` under `CHIMERA_GPU=1` when it is built with
  `-Dgl_bridge=true`. It had no case for `GL_OP_CONTEXT_ID`, the opcode that
  exists so `ChimeraCheckGLContext` can tell that the GL names it holds belong
  to a context that is gone (chimera issue #43), for as long as the opcode
  has existed. The default arm printed `opcode 4 has no case` and returned 0,
  and 0 is the contract's "cannot tell": the renderer concluded nothing had
  moved and kept the names. Chimera's real host (gl_bridge.cpp) answers the
  opcode, so the frontend was never affected - and neither was the
  `gl:rebuild-at-zero` leg or the #126 measurements, which go through
  chimera-run and that host. Only this repository's own harness had it.

  **And nothing here ever ran that harness.** `run-gate.sh` had no leg that
  put the GPU bridge up through run-wbx: every run-wbx leg draws with the
  softpipe, and the local build did not even compile the host half
  (`gl_bridge` defaults to false; the run-wbx in build/meson-native was 28 KB
  and printed nothing about a bridge). So the gap could not have been counted
  by anything. Measured once the host half was built, before the fix: 60
  frames of padtest.elf through run-wbx with `"renderer":"opengl-hw"` printed
  the line 60 times - once per advance - and with the case present the run's
  digests (video, audio, every domain) are byte-identical to the run without
  it. Nothing was lost from the command stream, only the answer to the one
  question that makes a restore safe. Absent was indistinguishable from
  working (chimera docs/gates.md, mode C).

  **The fix is rpcs3's, all three parts.** The case answers an id minted the
  way the engine mints it (pid and a high-resolution counter carry the
  per-process entropy; `time()` alone would hand two runs in the same second
  the SAME id); the host mints again on every state load through
  `chimera_gl_host_state_loaded`, which run-wbx calls after its `--rerecord`
  load, because chimera's host does (`ce_gl_state_loaded`; this core declares
  no `video.rebuildOnStateLoad`, so it rebuilds). The default arm COUNTS as
  well as logs, caps its own chatter at eight lines, and
  `chimera_gl_host_unhandled` hands the count to run-wbx, which prints it at
  the end. And `bridge_answered` in run-gate.sh fails a gpu leg when its
  stderr carries the line.

  **The leg it fails is new, because there was none: `gpu:bridge`.** It runs
  60 frames of padtest.elf through run-wbx with the bridge up and holds the
  dispatcher to having answered every opcode the guest sent. It compares no
  pictures - this runner has no native GL flavour (run-native draws with the
  softpipe) and llvmpipe is not a driver - and it SKIPs by name for a run-wbx
  built without the host half, a machine with no GL context, or a core.wbx
  built without a guest Mesa. The local build directory now has
  `-Dgl_bridge=true` configured so the leg runs here; a checkout that does
  not will see the SKIP and what to do about it.

  **Proved by breaking it.** With the case label changed to a number nothing
  sends and run-wbx relinked, the full gate said:

      gpu:bridge                   FAIL   the GPU bridge had no case for opcode 4 and answered 0 (gbridge.err)

  With the case back: `gpu:bridge PASS 60 frames of padtest.elf on 4.5
  (Compatibility Profile) Mesa 25.2.8 ... llvmpipe, every opcode the guest
  sent had a case`. Gate at the commit: 29 ok, 0 failed, 6 skipped (the six need
  discs, a GunCon disc or the NAMCO rom folders); the negative control was
  28 ok, 1 failed, 6 skipped, the one failure being the leg under test.

  **What this does not establish** (gates.md, E): padtest.elf on llvmpipe is
  neither a game nor a driver, and this leg does not look at the picture at
  all. It proves the dispatcher the harness hands a guest answers every
  opcode the guest sends - which, until today, nothing did.

- **2026-09-21** A stored context id of ZERO was read as "nothing to rebuild",
  and the frame-0 anchor is the one state that carries it (chimera issue #126).

  Reported on Maximo: Ghosts to Glory, Hardware-OpenGL, a 5070 Ti: a short
  tasproject replayed FROM THE BEGINNING draws a black screen with a sheared
  band of garbage, every time, with or without clearing the greenzone and
  across an emulator restart - and the reporter found the workaround
  themselves: **play it from frame 2 instead of frame 0 or 1**. Loading a
  branch state also clears it.

  **Those frame numbers are the whole diagnosis.** TAStudio reaches a frame by
  loading the state BEFORE it and emulating one frame forward, so that the
  destination has a picture rather than only a machine
  (`PriorStateForFramebuffer` is `States.Nearest(frame - 1)`). Frames 0 and 1
  therefore both load the state at frame 0; frame 2 is the first that does not.
  So whatever was wrong was wrong about the state at frame 0 and about nothing
  else.

  **What is wrong with it.** `ChimeraCheckGLContext` compares the context id
  stored beside the renderer's GL objects with the one the calls are landing
  on, and rebuilds the GS device when they differ. The stored id is a guest
  static that starts at 0 and is first written at the top of a frame advance.
  The greenzone's frame-0 anchor is taken right after Init and before any frame
  advance - the one moment in a session at which a GS device already EXISTS and
  the stored id is still 0 - and the guard `if (s_chimera_gl_context != 0)`
  read that 0 as "this machine has never held any GL objects". It does not mean
  that. It means "this state was taken before the core looked, so it cannot
  vouch for the objects the device is holding now", and those objects were
  whatever the frames after the anchor had left in the driver. The renderer
  then drew from guest memory describing frame 0 into driver objects as of
  frame N, for the rest of the run.

  **Measured, not reasoned.** `chimera-run --gpu --greenzone 4096
  --rewind-loop N,1` with `CHIMERA_GL_TRACE=1 CHIMERA_GL_STATEAUDIT=1`, on
  Maximo and on padtest.elf, counting the bridge crossings on the frame after
  the restore - a device rebuild is ~4500 calls, an idle frame of this program
  is 1:

  | restore to | before | after |
  |---|---|---|
  | frame 0 | **1 - no rebuild** | 4542 |
  | frame 1 | 4542 | 4542 |
  | frame 2 | 4542 | 4542 |
  | a fresh boot, no load at all | 4428 at frame 1 (the device's own setup) | the same 4428, no extra rebuild |

  **The fix keeps the host's word instead of guessing from the number.** The
  engine tells every core when the machine's memory has been replaced
  (`StateLoaded()`, an optional export this core did not have); the flag it
  sets is written AFTER the load, so the load cannot wipe it, and a stored 0
  seen after one is not trusted. A fresh boot has had no load and still does
  not rebuild, which the table's last row is there to hold.

  Two things were considered and rejected. Recording the id during `Init`
  instead would put it in the SEALED baseline, where it is not a delta any
  state carries - so a state made in one session would read the NEXT session's
  id and the cross-session rebuild (chimera issue #43, the reason any of this
  exists) would stop happening. And a non-zero "never seen" sentinel does not
  help either: it is the static's initial value, so the frame-0 anchor carries
  it just the same.

  **The leg is `gl:rebuild-at-zero`**, on padtest.elf so that it needs a bios
  and no disc: a restore to frame 0 and a restore to frame 2 must BOTH rebuild,
  because it was the difference between them that was the bug. It was run
  against the package built before the fix and FAILED there ("restoring the
  frame-0 anchor made 1 GL calls on the next frame, against 4542 restoring
  frame 2"), which is what makes it a test rather than a comment.

  **What this does NOT establish.** The repair is proven by the rebuild
  happening; the corrupt PICTURE was never reproduced here. On llvmpipe, with a
  short run, a frame-0 restore and a straight run give byte-identical frames -
  the audit says 0 objects deleted and 0 handed out again over 400 frames of
  this game, so the hazard the rebuild exists for never materialised on this
  box. The only evidence that the fix cures what the reporter saw is that it is
  the one thing that differs between the frames they say are broken and the
  frame they say is not. Nothing has been run on the reporter's hardware, and
  no NVIDIA driver has been near this.

  **Ruled out along the way, each by measurement rather than by reading.**
  - *The engine's seek path.* At engine level there is no difference at all
    between playing from frame 0 and replaying to the same frame after a seek
    to 2: `--frames 900` and `--frames 900 --rewind-loop 2,1` produce
    byte-identical final frames, and `video.drawEveryFrame` is already doing
    its job (a seek replays with the readback off and the drawing on).
  - *A warm-up or a lost paint.* Not this: the damage survives an arbitrary
    number of drawn frames after the start, which is the opposite of a picture
    painted once and skipped.
  - *The greenzone.* The reporter cleared it and the corruption stayed, and a
    GPU-drawn project writes no states across processes anyway, so the
    reopened session replays rather than restoring anything older.
  - *A cross-session state.* The corruption survives an emulator restart,
    which is a cold session with nothing carried in.
  - *`renderer` itself.* Nothing about the fresh boot's first frames differs
    between a run that later loads the anchor and one that does not: a fresh
    boot makes the same 4428 bridge crossings at frame 1 before and after this
    change.

  **Every other bridged core has the same hole.** Flycast's
  `chimera_check_gl_context` is the same code to the line and is unfixed; xemu,
  Dolphin, Ruffle and RPCS3 hold the same comparison and have not been checked.
  Written up in chimera's docs/gpu-bridge.md, where the contract lives.

- **2026-09-20** The NAMCO System 246 and 256 arcade boards (chimera issue
  #72). A System 246 is not another emulator: it is a Sony COH-H PlayStation 2
  with NAMCO's board bolted into the DEV9 expansion bay, so it belongs in this
  package as three more machines and not in a repository of its own. The
  evaluation that settled that is in the issue; this is what shipping it cost.

  **The board lives BESIDE upstream, not inside it.** `waterbox/arcade/` holds
  the eleven sources that are the board - the JVS I/O board, the ATA/ATAPI
  drive, the UART to a drive board, the settings SRAM, the board RAM and the
  interrupt core - taken from `PS2Homebrew-arcade/pcsx2x6` (GPL-3.0+, the
  licence this package already carries). They were WRITTEN to sit beside PCSX2
  rather than in it, and nine of the ten compiled against our pin unchanged, so
  putting them in our own tree rather than in the submodule is the whole
  difference between a pin bump that moves a handful of call sites and one that
  is a three-way merge. `chimera-arcade.h` is the only thing upstream sees.

  **Everything the board does is behind one runtime flag.** Patch 0023 adds
  five call sites: the IOP's bus dispatch (`chimera_arcade_present` is 0 for a
  PlayStation 2 and the hooks then read nothing at all), the `rom1:` mapping a
  COH-H declares at 0xB0000000 through `rom0:ACDEV`, and the EE and IOP clocks
  becoming the MACHINE's instead of a constant. That last one is the whole
  difference between a System 256 and a Super 256: 393.216 MHz against
  442.368, from the console's 294.912.

  **The board's RAM is allocated per machine, which the fork does not do.**
  Upstream's arcade fork makes the maximum 128MB a member of the IOP's memory
  struct, so every machine carries it - in its address space and in every
  savestate the greenzone keeps. Here it is a buffer sized by the `arcade_ram`
  setting, a System 256 has none, and a PlayStation 2 project pays nothing for
  a board it has not got.

  **A project states what the fork looks up.** Upstream reads a `.acgame` INI
  sitting beside the files, and a manifest on somebody's disk is not something
  a movie can cite. The machine, the NAMCO part number, the media type, the
  board RAM, the panel and the four DIP switches are settings; the dongle, the
  media and the boot program are slots. The one lookup kept is the panel
  wiring derived from the part number, because how a cabinet was wired is a
  fact about the game rather than a choice a project makes.

  **TWO THINGS HAD TO BE FIXED BEFORE TEKKEN 4 WOULD BOOT, and both were found
  by watching the boot rather than by reading the fork.** The board reached
  `mc0:ACCORE` - the first module NAMCO's own card manager loads off the
  dongle - and got "no such file", twelve times over, and then gave up to the
  bios browser.
  - The memory card's AUTH command (0xF3) put the card's terminator back to
    the default. MCMANAC, which the dongle carries and the boot loader runs,
    sets a terminator, authenticates, and then goes on talking to the card
    with the terminator it set. Not resetting it is what opened every module
    on the dongle: ACCORE, ACJV, ACRAM, ACSRAM, ACATA, ACCDVD and the rest.
  - `rom0:DAEMON` starts a security thread at priority 126 while the card is
    being read, and it races mcman for `mcman_io_sema`. Suppressing it is
    upstream's own workaround for an IOP scheduling shortfall, and this core
    needs it too: with it off, the game does not boot at all (7200 frames, all
    of them lag, three colours on the screen).

  Both are patch 0025 and both are arcade-only: a console's cards have been
  talking to that code for every movie this core has ever recorded.

  **Two other changes were tried and REVERTED because nothing needed them.**
  The fork also delays the SIO2 interrupt by the time the port really takes to
  shift its bytes, and stops a dongle being auto-ejected when the game serial
  changes. Both are plausible and neither made any difference to a game that
  boots: 7200 frames of Tekken 4 run with them and without them. A change to
  the machine that fixes nothing observable is a change that invalidates
  movies for nothing, so they are not here. If a game turns up that needs
  them, they are eleven lines each.

  **What is proven, on Tekken 4 (NM00004), System 246 Rack C bios:**
  1800 frames native == waterboxed, digest for digest; the machine ran (half
  the frames leave a different machine); a savestate round-trip around every
  one of 600 frames is lossless; a coin held for five frames at frame 2700
  changes the machine and changes it identically in the sandbox; and frame
  7200 is the attract-mode cutscene at 640x448, 89% lit with 75,101 distinct
  colours, drawn the same natively and sandboxed. The run writes nothing into
  the project: the board's settings memory leaves through the save-data
  channel as `sram.bin`, like a memory card.

  **What is NOT proven, and must not be claimed:**
  - **System 256 and Super System 256 have never been run.** The machines are
    declared, the clock is wired and the gate legs exist, and no System 256
    content has been near this machine. They are unproven.
  - **Only ONE game has ever booted.** The per-game JVS wiring covers 55
    titles and 54 of them are untested. The racing, drum, twin-stick, touch
    and light-gun panels have never had a game read them; only the coin and
    the Tekken layout have.
  - **The light gun on a board is untested.** It is wired to the movie's axes
    the way the console's GunCon 2 is, and no arcade gun game has run.
  - **CD and HDD media are untested.** Tekken 4 is a DVD; the CD path also
    hands the image to CDVD, and the HDD path has not been opened.
  - **The board's drive and its UART have not been through an equivalence
    gate of their own.** They were exercised only by this one game's loading.
  - **The System 256's regional signature is wired and untested.** The
    `arcade_region` setting reaches the mechacon's iLink read, which is where
    the Taiko games look for it, and no game has ever read it here.
  - **Speed is unmeasured as a claim.** 7200 frames of attract mode took 50
    seconds natively, which is faster than real time, but an attract mode is
    not a fight.

  **What each machine narrows.** A System 246 project may only pick a 246 or
  A-000-010 bios and has no iLink signature to set; a 256 or Super 256 may only
  pick the 256 dumps and has no board RAM. That is `settingOverrides` on each
  machine, which is the same mechanism a Master System uses to refuse a Mega
  Drive's mouse.

- **2026-09-19** The GunCon 2 (chimera issue 71). A PlayStation 2 light gun is
  now a device a port can be set to, and a movie carries where it was pointing
  like it carries a stick.

  **It is not a controller, and that is the whole shape of the work.** Every
  other device this core offers hangs off the SIO bus with the pads; Namco's
  gun plugs into a USB socket, and PCSX2 emulates it behind an OHCI host
  controller in `pcsx2/USB/` - a subsystem this core had stubbed out entirely
  ("two ports with nothing in them"), because a sandbox has no business
  emulating a webcam or a mass storage device. So the stub is gone and the
  bus is real: `USB.cpp`, the four qemu-usb files that are the host controller,
  and `usb-lightgun/guncon2.cpp`. What is still absent is the rest of the
  device registry, and it has to be absent explicitly: naming a device in
  `RegisterDevice::Register` is what COMPILES it, so a build that reached the
  gun through upstream's registry would carry a JPEG decoder, an audio device
  and a printer with it. Patch 0022 registers one device.

  **`port1`/`port2` gained `guncon2`, and the setting still means one PLAYER.**
  Choosing it puts a gun in the USB socket of that number and leaves the
  CONTROLLER socket of that number empty, which is what a real console with a
  light gun on it looks like. One setting rather than two because a project
  says what player 1 is holding; the number is kept as well as the choice,
  because a few games only look for the gun on USB port 2.

  **The aim comes from the movie, and it had to be taken away from the mouse.**
  Upstream asks `InputManager` where the host's pointer is right now and
  converts it out of the emulator window's coordinates. That is the one thing a
  movie cannot carry, so patch 0022 replaces both halves with a call back into
  this core: the frontend's two absolute axes, -32768..32767 laid over the
  picture, arrive as the 0..1 the window conversion would have produced. The
  off-screen shot - the trigger pulled away from the screen, which every
  light-gun game reads as a reload - is a declared BUTTON rather than a
  coordinate, because a movie needs a way to say it out loud.

  **A lag frame with a gun in your hands.** The lag hook lived in
  `PadDualshock2::Poll`, which never runs on a machine holding a gun: every
  frame would have been a lag frame. It now also sits where the gun answers its
  interrupt endpoint, which is where such a machine looks at its input.

  **The wire is keyed by NAMES, and a missing one is a refusal.** The gun's
  bind indices are an enum private to upstream's own file, so the core resolves
  its twelve controls out of `GunCon2Device::Bindings` by name at Init. A name
  that stopped matching would be a movie column the machine silently ignores -
  a recording that looks right and plays wrong - so the load stops and says
  which control is gone.

  **THE MOVIE FORMAT CHANGED AGAIN**, as it must whenever a physical port grows
  a device: 188 button columns and 44 axes, from 176 and 40. The two physical
  ports are 43 buttons and 10 axes each now.

  And the old trap caught the gate rather than the core this time: `pad:ports`
  pressed wire 44 for player 2's Cross because the second port's block used to
  start at 37. A slot's block is still not a multiplication, and a hard-coded
  base in a TEST is as wrong as one in the wire.

  **What this could not prove, and why it reports SKIP.** That the position and
  the trigger reach a GAME. Nothing on a PlayStation 2 polls a USB device on
  its own: a program loads the USB driver, opens the gun and asks it where it
  is pointing, and until then the gun is invisible to the machine's memory -
  measured, not assumed, and it is why plugging a gun into USB port 2 changes
  no digest at all. `padtest.elf`, the only program this repository may ship,
  reads the controller bus. `gun:aims` waits for one of the dozen or so discs
  the gun was built for.

- **2026-09-11** `renderer` defaults to `software` again. The hardware path is
  the one that has gone wrong in use - the re-recording picture degradation
  reported as chimera issues 55 and 56, and on Windows the fact that a build or
  a machine that cannot give the bridge a context falls back to a Mesa softpipe
  which dies after about a thousand frames - and none of that can reach a
  rasteriser that runs inside the sandbox. The cost is measured rather than
  guessed. 2401 frames of Gran Turismo 4 on a GTX 1060: 20.55s and 21.01s for
  the software rasteriser against 9.63s for `opengl-hw`, so a little over twice
  the time. What that buys is a picture that is a promise: the two software runs
  are byte-identical at frames 900, 1500 and 2400, and a third run with no
  settings override at all - the new default doing the choosing - matched them
  byte for byte as well. And the machine never noticed which renderer drew: the
  EE RAM dumped after 2401 frames is the same under software and under
  `opengl-hw`. `opengl-hw` is still there, and still one setting away.

  The setting's own description needed the same repair: it said "'software' ...
  and the default" in its first sentence and "It is the default" about
  `opengl-hw` four sentences later, because the default was flipped once before
  and the prose was left behind. It is rewritten, and the claim that software is
  "about five times faster than the other two" is gone with it - true of the
  softpipe, and exactly backwards against a GPU, which is why the number
  measured above is written down instead.

- **2026-09-11** A report that the OpenGL hardware renderer shows a pitch-black
  picture did NOT reproduce here, and `drawEveryFrame` is not what it is. The
  A/B is direct: the installed package was repacked with `drawEveryFrame` set
  to false and run beside the real one, same frontend, same movie, same
  destination, and the two pictures agree - so the flag added earlier today
  neither causes a black screen nor hides one. Nor does anything else, on this
  box: headless with and without the readback, a twenty-pass rewind loop, the
  frontend headless and in a real window and fullscreen, a bare disc and a
  project with the piano roll open, a core rebooted twice in one process. All of
  them drew. The one thing that did come out black every time was a frame that
  is black in the movie anyway, which is worth saying out loud because it cost
  two false alarms: check the ground truth for the frame AND for the input
  before believing a black screenshot.

- **2026-09-11** `renderWarmupFrames: 10` withdrawn; `drawEveryFrame: true` in
  its place. Yesterday's warm-up was measured on Marvel vs Capcom 2, which
  redraws its whole screen every frame, and there five drawn frames really are
  enough to put the display stage back where a straight playback would have left
  it. Gran Turismo 4 is not that game: at three different points in its boot the
  ten declared frames still left 1.3% to 2.8% of the picture wrong, and Flycast
  found the unbounded case outright - a title screen painted by ONE frame is
  simply lost if that frame is skipped. So the core is no longer told to stop
  drawing at all; turbo skips the readback and nothing else. 1500 frames of
  Gran Turismo 4 on a GTX 1060: 8.28s turbo, 8.30s drawing, 9.75s drawing AND
  reading back - the drawing is 0.017 ms a frame and the readback 0.98 ms, so
  this gives up 0.3% of a seek's speed to be exactly right. Three rewinds at
  frames 900, 1500 and 2400 are now byte-identical to a straight run.

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
- **The light gun's last link is unproven.** A GunCon 2 is on the USB bus, its
  twelve controls reach the emulated gun and a machine holding one is
  byte-identical in the sandbox - but nothing here has watched a game read the
  position off it, because no disc built for the gun is on this machine and a
  PS2 polls USB only once a program has loaded the driver. The gate says so
  (`gun:aims` SKIP) rather than implying otherwise.

- **The pressure modifier is not offered.** A DualShock 2's buttons are
  pressure-sensitive and `PAD_PRESSURE` scales how hard the host is pressing
  them. It is a host convenience rather than a control the machine has, so it
  is left out; a movie that wants half-pressed buttons has no way to ask yet.

## Sharp edges hit

- **A light gun is not answered by pointing it** (2026-09-20, chimera#71). With
  Time Crisis II on the disc, aiming at the target and pulling the trigger
  changed the machine's memory and never changed its screen: the calibration
  screen sat there through two hundred frames of held trigger. Upstream's own
  comment says why - a Time Crisis game calibrates by waiting for the gun to
  report (0, 0) once a shot is fired, and the emulator produces that sequence
  only when the RECALIBRATE control is pressed (`calibration_timer` in
  guncon2.cpp). Press recalibrate, then the trigger, and the screen answers:
  the target changes and the game walks on to its memory card prompt. The
  `gun:aims` leg does exactly that, and fails if the machine moves while the
  picture does not - which is the shape this bug had.
