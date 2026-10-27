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

- **2026-10-15** Dithering (chimera issue #283): `dithering` sets PCSX2's own
  `dithering_ps2` under the OpenGL and Vulkan renderers - `unscaled` (2,
  PCSX2's default), `scaled` (1), `off` (0), `force32` (3); the software
  rasteriser dithers as the GS does and never reads it. A sync setting for
  Texture Filtering's reason. Measured on Street Fighter EX3, 1500 frames on
  llvmpipe: the default, `off` and `force32` draw three different pictures
  over the run, and the memory domains, the sound and the lag count are byte
  for byte the same in all three. The gpu:picture leg now has `off` and
  `force32` beside nearest, fxaa and 2x (one machine, six pictures); with the
  assignment taken out, `off` drew the default's picture. `scaled` differs
  from the default only above 1x and is not in the leg.
- **2026-10-09** The frames after a load are the pictures they were (chimera
  issue 190; `waterbox/gs-shadow.cpp`, patch 0026). Measured on a GTX 1060,
  Street Fighter EX3's 3D intro at frame 3600: the first frame after a load
  was an empty one, the three after it were off in 6 to 8% of their pixels,
  and the fifth was right. Maximo and Marvel vs. Capcom 2 the same, a still
  screen included.
  - *Why.* Under the OpenGL renderer a render target is a texture on the
    card, and the GS's own memory under a frame buffer holds nothing: the
    hardware renderer writes it there only when a game reads it. A state is
    this core's memory. A load gives the renderer a new GL context, and the
    rebuild - GSreopen's "we lost the device" - threw the texture cache away
    and made targets again from that memory. The frame the game had drawn and
    not yet shown, the depth it had not yet cleared, and the four fields the
    deinterlacer keeps (its motion-adaptive buffer, which is why it took four
    frames and why a still screen was wrong too) were gone.
  - *The answer.* The core answers the engine's `StateSaving`, told before
    every state is taken: each target of the texture cache, colour and depth,
    and the deinterlacer's textures in the device are read - in the format
    they are held in, band by band - into a block of the core's own memory,
    which a state carries. A texture that did not change is compared and not
    written again. After a load the rebuild keeps the cache's targets (its
    sources, palettes and hash cache go, as before), lets go of every texture
    of the old context BEFORE the device is opened again, gives each target a
    new one and puts its pixels back; the deinterlacer's likewise. A target
    with a clear pending holds the clear in the object and is given it back,
    not read. One the block has nothing for goes, as all of them used to.
  - *Not into the GS's memory*, where upstream's own "read targets when
    closing" hack puts them. A game can read that memory and the renderer
    falls back on it when a target goes, so a run that took a state would
    hold other bytes there than a run that did not. And no flush: what the GS
    has queued at a frame's end is vertices in this core's memory, drawn
    later into the targets as they come back.
  - *Proved on the card, at the machine's own resolution:* twelve frames
    after a load, each bit for bit the picture it was, in all three games -
    through the greenzone, by a whole state, onto an even frame and an odd
    one, from states of frames nobody read, early in the boot and deep in the
    intro. With the core left untold (`CHIMERA_NO_STATE_SAVING=1`) the first
    four are wrong, as before: the control. Eight rewinds of 250 frames end
    on the picture and the EE RAM of a run that never took a state, and a
    state on every frame leaves the same picture and RAM told and untold.
  - *What it costs.* In that scene six textures, 8.7 MB, nearly all of which
    change every frame: a stored state is that much larger (a mean delta of
    15.7 MB against 4.2), the history spaces itself out - 1136 states where
    it kept 3701 over 3700 frames - and the run takes 40 s where it took 35.
    It grows with the square of `internalResolution`: 78 MB at 3x.
  - *What does not come back above 1x.* Drawn larger than the PS2 drew, a
    sprite's edge samples a texel past what its texture was loaded with -
    upstream's well-known upscaling lines - and what is there is whatever the
    recycled texture held before, the history of the device's texture pool. A
    new context's textures held nothing. At 2x 0.12% of the picture's pixels
    differ after a load and at 3x 0.03%, on its edge rows, for as long as the
    scene lasts - where it was 0.34% at 3x after four frames far worse.
    Carrying the device's pool of spare targets as well was tried: it moved
    those pixels and did not remove them.
  - *Not carried:* a texture the 768 MiB block has no room for or whose
    format the copy does not know, and the textures of an effect in flight
    between two draws (the colour clip target), which are not the cache's.
  - *The gate:* `gl:picture-after-load`, padtest.elf through llvmpipe, six
    frames exact after a load and the untold control wrong for four.
    `glGetTextureSubImage` joined this core's list of GL entry points.
- **2026-10-02** Internal resolution, 1x to 4x (chimera issue #170, approved by
  Sergio): `internalResolution` sets PCSX2's own `upscale_multiplier` under the
  OpenGL renderers; the software rasteriser ignores it. A sync setting for the
  same reason Texture Filtering is: GSTextureCache::Read scales an upscaled
  target back down when a game reads its picture, and that need not be the
  native bytes. Measured on Gran Turismo 4, 900 frames on the 1060: 2x draws
  1280x896 where 1x draws 640x448, and EE RAM is byte-identical; the gate's
  gpu:picture leg now proves it alongside nearest and fxaa (one machine, four
  pictures).

  The frame buffer grew to 2560x2048 (a frame at 4x, and a 1080i frame at 1x,
  which the old 1280x1024 cropped) and went INVISIBLE, as did the OpenGL
  readback that used to be a heap vector: neither is the machine, and the old
  buffer put 5 MB of picture into every savestate. A frame larger still (a
  high-resolution mode at 3x or 4x) is shrunk on the GPU with PCSX2's own
  StretchRect before it is read; reading it in bands needs
  glGetTextureSubImage, which the GPU bridge does not carry (tried: the call
  went to address 0). That path was proved by forcing a 1000-wide limit: the
  2x frame came back at 640x448, the same picture, the same EE RAM.

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
  - **Super System 256 has never been run, and System 256 has run one game.**
    Super Dragon Ball Z (NM00027) reaches its notice screen since 2026-09-23
    (chimera#136, below); nothing on it has been played, and no Super 256
    content has been near this machine.
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

## The Vulkan renderer (user-decided, 2026-10-11, chimera#248)

The owner decided that every core whose emulator has a Vulkan renderer offers
it. Here that is a fourth value of the Renderer setting, `vulkan-hw`: PCSX2's
Vulkan renderer, drawing on the computer's graphics card through Chimera's
Vulkan bridge. The default did not change. It has nothing to fall back on: on
a computer that hands no Vulkan over, Init refuses and says why.

What it took (patch 0027, six files of PCSX2, and the adapter):

- **The functions.** Upstream opens the Vulkan library and asks it for each
  function. The bridge's guest half is linked into the core instead, and
  `VKLoader.cpp` asks it by name.
- **The shader compiler.** The renderer writes GLSL and needs SPIR-V.
  Upstream asks shaderc, a shared library loaded at run time; a sandbox loads
  nothing, so glslang - the compiler shaderc wraps - is a submodule
  (`extern/glslang`, 14.3.0), compiled in without its optimiser and without
  HLSL, and asked directly. What it compiles is kept by source, because the
  device is made again after every load and asks for the same shaders. The
  shaders themselves, and the two FidelityFX headers they include, are
  compiled into the core (`waterbox/gen-shaders.py`).
- **Push descriptors.** PCSX2 hands a draw its textures with
  `vkCmdPushDescriptorSetKHR`. Chimera's bridge carries that command from
  miniBox a348f60 (Chimera 4ca6e30) on; a core built against that list is
  refused the bridge by an older Chimera, and then says so at Init.
- **No window.** The frame is fetched with PCSX2's own download texture
  (`ChimeraGSDownload` in `waterbox/gs-device.cpp`), where the OpenGL path
  asks the driver for the texture's pixels.
- **Memory.** Through the bridge every mapped byte is a byte of the core's
  memory, copied when the mapping is made, and no memory type is coherent:
  what is written has to be flushed and what is read invalidated. Upstream
  flushes, and invalidated one row of a download where it reads them all
  (the picture was black until that was all rows). Buffers that are read or
  written through a mapping ask for cached memory, not write-combined. And
  less is mapped: the stream buffers have the OpenGL device's sizes (24 MiB
  in all against 128), and the allocator's blocks are 16 MiB.
- **A load.** A load is a new Vulkan device. The adapter's check
  (`ChimeraCheckGLContext`, now for either bridge) rebuilds the GS device as
  it does for OpenGL. While PCSX2 lets go of the old device none of its
  commands cross the bridge (the adapter's `ChimeraVulkanBridge` answers
  "done"), and `CloseGSDevice` says when the old device is gone
  (`ChimeraOldDeviceGone`). The render targets and the deinterlacer's fields
  are copied into the core's memory before a state and given back after a
  load by the same code as for OpenGL (`waterbox/gs-shadow.cpp`), which
  downloads and uploads through PCSX2's device where it used OpenGL calls.
- **No pipeline cache file.** Upstream writes the driver's pipeline cache to
  a file when the device closes. In a sandbox that file is part of every
  state and its bytes are that computer's driver's; and after a load the
  device that would answer is gone - the size was never filled in, and the
  core asked for 3,600,431 MiB and died. The Chimera build keeps the cache
  in memory only.

The core's own sandbox runner (`run-wbx`) now tells the core before each
state and after each load (`StateSaving`, `StateLoaded`), as the frontend
does. It did not, and a load before the first frame went unnoticed.

Measured on Mesa's lavapipe (2026-10-12):

- padtest.elf, 300 frames: the five memory domains, the sound and the lag
  count are the software renderer's, and no pixel of frame 300 differs from
  OpenGL's by more than 8 levels of 255. A state saved and loaded around
  every frame is the straight run, picture included, with no command refused.
- Maximo: Ghosts to Glory, 2400 frames from power-on: the machine is the one
  OpenGL ran; 18,177 of 1,146,880 bytes of the last picture differ, 2 of
  them by more than 8 levels. A state saved and loaded around every one of
  the 2400 frames is the straight run, picture included (617 s against 18).
  Texture filtering, FXAA and 2x internal resolution each change the picture
  and not the machine.
- Six frames drawn right after a load are bit for bit the pictures they
  were; with the core left untold before the state, three of the six are
  not.

Measured on a GeForce GTX 1060 (Windows, driver 581.42, Vulkan 1.4.312)
through chimera-run.exe, with no input, from power-on:

- Street Fighter EX3, 3700 frames (frame 3699 is in a fight): 13.6 s with
  Vulkan, 13.8 s with OpenGL. EE RAM is identical, and no pixel of frame
  3699 differs by more than 8 levels. With every frame fetched: 16.6 s
  against 19.0 s. At 2x: 12.6 s against 12.7 s, EE RAM still identical, 508
  of 1,146,880 pixels differ by more than 8 levels.
- Twelve frames drawn after a load, compared with the frames first drawn:
  all twelve exact on Street Fighter EX3 at frame 3600, Marvel vs. Capcom 2
  at 3600 and Maximo at 2400 and 4200. With the core left untold, the first
  four are wrong on each.
- Street Fighter EX3, eight times back from frame 3700 to 3450 and forward
  again: the last picture and EE RAM are the straight run's, byte for byte.
  It took 30.2 s against 28.0 s with OpenGL (15.5 s and 15.2 s without the
  going back): a load costs about a quarter of a second more with Vulkan,
  of which making the device is 44 ms.

Not measured: a game played with input, anything above 2x, and any card other
than this one. `vk:` checks in the test script run on lavapipe, which is not a
graphics card.

## The GS's floating point (2026-10-13, chimera#257)

Black drew a white haze over everything far away under both hardware
renderers, at every internal resolution, and was right under the software
renderer. PCSX2 itself draws it right with the same source this core builds:
checked by writing a GS dump of the scene from this core (four frames of the
GS's own commands) and replaying it in upstream's release v2.7.524 with its
OpenGL renderer on the same software driver (llvmpipe). Upstream: clean.
This core's OpenGL renderer replaying the same dump: the haze.

**What it was.** The thread that emulates the console's CPU runs with the
PlayStation 2's floating point rounding: toward zero, denormals flushed.
Upstream's GS has a thread of its own, and the first thing that thread does
is put the default rounding on - its comment says the console's "appears to
get inherited" otherwise. Here the GS runs on the CPU's thread (patch 0004,
"the GS runs inline"), and nothing put the default on. So the hardware
renderers computed with the console's rounding: a texture scale of 1.6 came
out as 1.5999999, a vertex scale of 0.0001953125 as 0.00019531249. Found by
making both programs write upstream's own per-draw configuration dump
(`SaveHWConfig`) for the same twelve draws: everything was the same except
those numbers, each one step low.

In Black it shows because the game copies the top bits of its depth buffer
into the picture's alpha, texel for texel, and then lays fog over the picture
through that alpha. With every coordinate a hair off the copy took the
neighbouring half of each depth value: the top bit came out zero for every
pixel (read back from the card: 196,032 and 131,648 pixels in the two lower
quarters of the alpha range, none in the upper two, where the software
renderer has 86,880 and 48,079).

**The change** (patch 0004, `MTGS.cpp`, and `waterbox/cinterface.cpp`). The
GS's rounding is put on for as long as the GS runs - while it drains its
commands, opens and closes, and where the core asks the renderer for
something directly (the targets copied out for a savestate and made again
after a load, the frame read back) - and the CPU's is put back after. The
second half was found by the tests: with only the first, a picture given
back after a load under Vulkan was no longer bit for bit the one drawn. The core says once which rounding it found and
which it runs with ("the GS runs with floating point control 00001f80; the
thread it runs on had 0000ffe0").

**It is not only Black, and not only the hardware renderers.** Every sum the
GS does in floating point was done with that rounding, the software
renderer's included, so pictures can change by a pixel's worth anywhere. A
game that reads its picture back reads those pixels: a movie made with an
earlier build may not play back the same.

**Tested.** The game itself on a GeForce GTX 1060, at the window the
report's scene looks out of: Vulkan at 1x and at 4x and OpenGL are the
software renderer's picture to within a level or two of mean colour (blue
45.1 against 45.3; before, 52.5), and Vulkan and OpenGL are the same file.
The dump replayed in this core's OpenGL renderer after the
change is upstream's picture (mean colour 82.9, 76.8, 45.6 against
upstream's 83.4, 77.3, 46.2; before: 85.3, 79.9, 53.0), and the alpha read
back from the card has the software renderer's counts. New check
`gs:rounding` reads the core's line in both flavors; with the change taken
out the line reads `0000ffe0 0000ffe0` and the check fails. The test
script: 42 passed, 0 failed, 5 skipped (a light-gun disc, a graphics disc
and the three arcade boards are not named on this machine); through
Chimera: 3 passed, 0 failed, 1 skipped.

Asked about and not the cause, each tried on the scene: PCSX2's Blending
Accuracy at "full", its downloads from the card switched off, its per-game
fixes switched off, the SSE4 code path against AVX2.

## The compatibility patches are on (user-decided, 2026-10-13)

PCSX2's game database carries, for some two hundred and forty titles,
corrections to the game's own code: "patches", written to memory when the
program is loaded and again every frame, and for six titles "dynaPatches", a
short pattern of instructions replaced wherever the recompiler meets it.
PCSX2 applies them unless a person turns them off. Black's is four words -
"COP2 Rearrangement. Fixes broken collisions."

This core refused them from the start, on the grounds that a movie must not
have the game rewritten behind its back. The owner's decision, when Black
came up (chimera issue 257): "On, as on upstream. We've already done this
for some cores." The argument that settled it is the one already made for
the rest of the database: it is compiled into the core, so the package a
movie cites pins the patches exactly as it pins the clamp modes.

**What changed.**

- `waterbox/gen-gamedb.py` carries `patches` and `dynaPatches` (they were on
  its list of keys left out). A patch's text is one line with its newlines
  escaped. Keys are read as text: one checksum in the file is all digits
  (SLES-50580's `33611180`) and the YAML reader made a number of it.
- `waterbox/game-database.cpp` reads them into upstream's own structure, with
  upstream's own rules for a bad or a repeated checksum.
- PCSX2's `Patch.cpp` is compiled, and the stand-ins that answered for it
  are gone. Patch 0028 keeps it from reading any patch from a file: upstream
  also looks in a folder of patch files and in `patches.zip`, which is where
  cheats, widescreen and the patches a person chooses live. A core has
  neither, and a file is not part of the package.
- `EnablePatches` is on. Cheats, widescreen patches and no-interlacing
  patches stay off.

There is no setting for it. Every movie of a title that has a patch plays
differently from this build on; a project made with an older build keeps
that build.

**Tested.** Two new checks. `gamedb:patches` needs nothing: a machine with an
empty tray says Black carries one patch and Ratchet & Clank 2 (SCES-51607)
one pattern, in the native build and in the sandbox. `patch:memory` needs a
BIOS: the core is asked what Black's patches do to the word at 0037EBD4
(`patch_probe`, a question like `gamedb_probe` and not a setting), loads them
the way PCSX2 does for a disc, applies them, and answers
`before=00000000 after=4b000460` in both. With `EnablePatches` off it
answers `after=00000000`. The test script: 44 passed, 0 failed, 5 skipped
(the same five as before: a light-gun disc, a graphics disc and the three
arcade boards are not named on this machine); through Chimera: 3 passed, 0
failed, 1 skipped.

And the game itself, Black (Europe), 1,500 frames through Chimera's runner:
the core's log says "Found 1 game patches in GameDB", and the four words in
the console's memory are the patch's (`4b000460 4af103bc` at 0037EBD4,
`4a800460 4b7103bc` at 0037EBF0) where the published build has the game's
own order. Nothing else in the 32 MiB differs at that frame.

**Not proven:** that the collisions are right now - nobody played to a place
where they were wrong. No title with a dynaPatch was run; that road is
upstream's own call from the recompiler and is only known to link.

## A game on several discs (2026-10-15, chimera#286)

The owner decided that a PCSX2 project takes several discs, with the
controls the other cores with discs have (user-decided, 2026-10-15). The
Disc slot takes up to eight files; the first is started. After the eight
slots' controls come three of the console's own: Previous Disc and Next
Disc move a selector over the project's discs and touch nothing, and Disc
Swap is the tray, open while held and closing on the selected disc, or on
the next one when the selector was not moved (DuckStation's design, its
`machine-console.inc`). Their keys out of the box are [, ] and \.

Open, the driver asks PCSX2 for no disc; released, for the selected one,
both through PCSX2's own Change Disc (`VMManager::ChangeDisc`), which also
reads the new disc's serial and so its settings and patches. That change
keeps the tray reporting open for a second and then detects the disc for
three, as a console's drive takes its time. A file that is no disc leaves
the drive empty and says so. The selector and the tray live in the core's
memory, so a state carries them. An arcade board has none of this.

Measured on Street Fighter EX3 with Marvel vs. Capcom 2 as the second
disc: the tray open from frame 300 to 360 gives another machine at frame
1200 than no change, the same natively, in the sandbox and with a state
around every frame; a second file that is no disc gives a third. At frame
600 the second disc had not been read yet, so the test runs 1200. No game
on two discs was played.

## A board's S/PDIF output is heard (2026-10-16, chimera#226)

Time Crisis 4 on a Super System 256 had its music and none of its effects
or the commander's voice. The reporter's video of the standalone PCSX2x6
fork has them. The game sends them through core 0's input to the sound
chip's S/PDIF transmitter, which on the cabinet feeds an EXSOUND board,
and upstream PCSX2 silences core 0's output while the chip is in S/PDIF
mode (on a console that output may be a compressed bitstream nothing here
decodes). The fork mixes the transmitter's input into core 0's output
instead (pcsx2x6 c45a96b, "SPDIF output path for TC4 EXSOUND board"):
read at twice the ADMA rate as packed 32-bit words, low half left and high
half right, without the input volume, with the master volume. Patch 0029
does the same on an arcade board only (`chimera_arcade_present`); a
console keeps upstream's silence.

Measured: Super Dragon Ball Z on a System 256, 3600 frames, is the same run
before and after (every digest, audio included), so a board game that does
not use S/PDIF is not touched. Time Crisis 4 (the reporter's hard disk
image and dongle, by SHA-1), with two coins at frames 1000 and 1100, Start
at 1300 and 1700, and the trigger pulled every 200 frames from 10000:
the chip is in S/PDIF mode for 114 of the run's 260 seconds, and what the
transmitter takes is a voice with its harmonics and the sharp bursts of
gunshots, band-limited at 24 kHz - sound, not a bitstream, which would be
noise across the whole band. With the path switched off (a local build)
the run's sound is another and has none of it. Not listened to by a
person.

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

- **A System 256 needs more than 32MB of EE RAM** (2026-09-23, chimera#136).
  Every 256 game died at a black screen, one of them with PCSX2's `FQC = 0 on
  VIF FIFO READ` assert. The boot program (proverb) calls SetupThread with a
  stack of -1, and this bios's kernel answers with a stack at the top of 64MB -
  0x03ffecc0 - on an EE that had 32. The first return address read back from
  that stack is zero, the program jumps to address 0, and what follows is
  noise: the recompiler wanders into the kernel's argument copier with an
  address for a count and strcats 7MB of memory until it reads the VIF FIFO;
  the interpreter spins in the kernel instead. Both were symptoms. The fork
  turns PCSX2's extra memory on for every arcade game; this core turns it on
  for the two 256 boards only, because a System 246 boots in 32MB and its
  movies were made there. What found it was a syscall trace (nothing between
  SetupHeap and the crash) and then the first user-to-kernel transition after
  it, which showed a jump from 0 with the stack pointer above 32MB.

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

- **Invisible memory is taken during Init, never on first use** (2026-10-07,
  chimera#217). The buffer an OpenGL frame is read back into was taken from
  the invisible heap the first time a frame was read back. What a state load
  leaves alone in that heap is the CONTENTS of its pages; whether a page is
  mapped at all is in every state, and so is the allocator's cursor. So
  loading a state made before that first read-back - the frame-0 anchor -
  unmapped the buffer under a pointer that, being invisible, kept pointing at
  it, and the next read-back was the GPU driver writing into nothing: in host
  code, so the process went and not the machine. The report said "4x
  resolution"; it is any resolution under the hardware renderer, on going
  back to the start. It also grew: a state loaded by a new process before its
  first frame carried the cursor past what the last process took, 20 MB a
  reopening out of 64. `gl:rebuild-at-zero` could not see any of it, because
  chimera-run reads a frame back only when asked and that leg never asks;
  `gl:readback-after-zero` asks for every frame, and against the build before
  the fix it is a segmentation fault.
- **The cabinet has a Test button as well as a Test switch** (2026-10-07,
  chimera#219). The I/O board reports the two in one bit, and the fork's code
  keeps a variable for the button that nothing ever set, so the DIP switch
  (the `dip_test` setting) was the only way into a test menu and a game that
  waits for Test to be pressed again never left it. `Test` is the last line
  of the Arcade Panel. On Super Dragon Ball Z, held for thirty frames, it
  brings up the maintenance warning; pressed again, the test menu.
  `<board>:test-button` holds it and requires another machine than idle and
  than a coin, the same in both flavors; with the button unwired the run is
  the idle one.
- **The two faster boards state their own sound rate** (2026-10-10,
  chimera#226). Reported as sound problems in Time Crisis 4 and Tekken 5
  Dark Resurrection. A System 256 runs every clock a third faster than a
  PlayStation 2 and a Super 256 a half faster, and the sound chip counts its
  samples on that clock: 49.152 MHz over 768 is 64000 samples a second,
  55.296 MHz over 768 is 72000. The fork says the same (its console sample
  rate is the clock over 768). The package said 48000 for every machine, so
  Chimera played a third to a half too many samples at the console's rate.
  Measured over 600 frames of Super Dragon Ball Z: 637952 samples on a
  System 256 and 717760 on a Super 256, where 48000 a second is 480480. The
  game agrees with the chip's rate: started with a coin it programs voice
  pitches 0x0C00 and 0x0480, which are 48000 Hz and 18000 Hz at 64000 and
  would be 36000 Hz and 13500 Hz at 48000. The fix is in the declarations:
  `"audioRate": 64000` and `72000` in the two machines' entries, a
  declaration Chimera has from commit 2385e5e. An older Chimera ignores it
  and plays those boards as before. The test program prints
  `audioSamples`, and `<board>:audio-rate` checks it against the rate the
  package states for that machine, within 2 percent. Not tested: the two
  games of the report, which are not on the test machine.

## 2026-10-14: the default bindings name keys Chimera sends

L2 and R2 were on 1 and 2, the arcade panel's coins and drums on 5 to 0,
Select and Service on RightShift alone, and a trigger on LeftControl. They are
on Number1 and so on, Shift and RightShift, Ctrl and LeftCtrl. Chimera hands a
key to the bindings under one name: the name its DistinctKeyNameOverrides
gives it ("Number1", "Enter", "Backtick", "KeypadAdd") or else its DistinctKey
name ("Up"); and unless told otherwise it reads the left and right Shift, Ctrl
and Alt as one key each, "Shift", "Ctrl" and "Alt" (Input.cs,
Config.MergeLAndRModifierKeys). A default binding to another name never fired,
so those controls shipped unbound on the keyboard. Checked on a virtual X display: with 1,
Return, left Shift and Up held, Chimera reports Number1, Enter, Shift and Up.
Left and right names stay beside the merged one, for a setup with the merging
off.

## 2026-10-16: the Boot ROM choices are shown by their names (chimera#293)

The Boot ROM setting's values are BIOS file names such as
`ps2-0230a-20080220`, and nothing said that the `a` means USA. Both Boot ROM
settings (the console's and the arcade boards') now carry `optionLabels`, a
package field Chimera added in chimera 1e723d6: each value is shown as the
label of the firmware entry it calls for, "USA v2.30 (2008-02-20)". Only the
words change; projects and movies keep the value, and an older Chimera
ignores the field. The labels were generated from the firmware entries, so
every one of the 73 console values and 4 arcade values has one. In the same
Chimera change, the wizard lists first the Boot ROMs whose file is on hand,
marked "(found)".

## 2026-10-19: an open BIOS and a program of our own, so CI runs the machine (user-decided)

Until now the CI could not run a single instruction of the machine: a PS2
needs a BIOS, and a retail one is somebody's dump. OpenPS2BIOS
(TamaribaEmu/OpenPS2BIOS, MIT, no Sony code, made for emulators and tested in
PCSX2) changes that. The owner chose it as a pinned submodule built in CI
(not a download), and a homebrew disc of our own in the repository, with its
licence.

- **The BIOS.** `extern/openps2bios`, pinned at the v0.2 release tag.
  `waterbox/build-openps2bios.sh` builds it with LLVM 23 - what upstream
  builds its releases with - and checks the result against the SHA-256
  upstream published: LLVM 23.1.4 gives the released bytes exactly, LLVM 20
  differs in 170,054 of 4 MiB (and needs its unknown `nonstring` attribute
  ignored). CI installs LLVM 23 from apt.llvm.org. The gate falls back to it
  when no retail BIOS is found; it is not in the package.
- **The program.** `tests/own/chimera-padtest` (MIT; linked with ps2sdk and
  gsKit, AFL-2.0, and newlib): a cell per button for both ports, lit while
  held, and a bar that grows a pixel a frame, drawn into one framebuffer once
  and then only where it changes, so a renderer that loses the GS's memory
  across a load shows it. Built with the ps2dev/ps2dev v2.0.0 image (unpacked
  and run chrooted in a user namespace, no Docker here); the ELF and the disc
  build byte for byte the same each time. It replaces `padtest.elf` (no
  licence for passing it on) in every check that ran it.
- **What OpenPS2BIOS needed of the program** (each found by its console
  output, `"verbose":true`): ps2sdk's libc start-up opens the IOP's file
  services over SIF before main and never returns there, so it is left out
  (nothing here uses files); ps2sdk's patch that lets a retail BIOS load a
  module from memory never returns on OpenPS2BIOS, which can already, while
  a retail BIOS never answers a load without it - so the program looks for
  OpenPS2BIOS's name in the ROM (read through the kernel, kCopyBytes; reading
  it directly is refused there) and patches only a retail BIOS. It loads
  ps2sdk's own pad drivers (freesio2, freepad) from inside itself, so the
  pads read alike on both.
- **Measured** (this machine, CHIMERA_ROOT my own worktree): with the retail
  BIOS 44 passed, 0 failed, 6 skipped - the same as before with padtest.elf;
  on OpenPS2BIOS 38 passed, 0 failed, 13 skipped. The seven that need a
  retail BIOS say SKIP with why: `input:shaped` and `input:lag` (OpenPS2BIOS's
  own boot screen does not read the pad), `savedata:roundtrip` (no memory
  card read back), `pad:elf` (it starts discs only, not an executable),
  `discs:swap`, `video:deinterlace` and `video:steady` (its boot screen is
  not interlaced). Seen to fail on the way: the pad checks with the program
  black (before the start-up was fixed), and `ports:columns` and the two
  picture-after-load checks on the first version of the program (a disc adds
  the tray's column; a picture redrawn whole every frame hides a lost one),
  both caught by Chimera-Desktop's run.
- An earlier comparison of the two BIOSes ran Chimera's runner from
  `~/chimera`, a peer's uncommitted build rebuilt during the run; its numbers
  are not used here.

Not decided: whether `padtest.elf` stays in `tests/own` (nothing runs it now),
and whether the package should accept OpenPS2BIOS as firmware, which would let
the frontend test run on CI too and would show it to users.

Later the same day the owner had CI run the GPU bridge checks and the ones
that open a project with `chimera-run` too. The core gate job now builds
`run-wbx` with `-Dgl_bridge=true`, installs Mesa's software OpenGL and
Vulkan (the runner has no graphics card), builds Chimera's engine and the
package, and sets `CHIMERA_ROOT` to that Chimera. That adds `ports:columns`,
`gpu:bridge`, `vk:bridge`, `vk:machine`, `vk:picture`, `vk:rerecord`,
`gl:rebuild-at-zero`, `gl:readback-after-zero`, `gl:picture-after-load` and
`vk:picture-after-load`, which all passed here on OpenPS2BIOS (with
lavapipe for Vulkan). On the runner they draw in software, so they prove the
bridges and the rebuild after a load, not a card's picture.


## 2026-10-19: OpenPS2BIOS is a Boot ROM the package accepts, and padtest.elf is gone (user-decided)

The owner answered the two questions left open above.

- **`padtest.elf` is removed** from `tests/own`, with `waterbox/tests/pad-cells.py`,
  which read its picture. Nothing ran either since chimera-padtest replaced
  them, and no licence was found that let us pass the file on.
- **OpenPS2BIOS 0.2 is a choice of the Boot ROM setting** (`openps2bios-0.2`,
  last in the list), with a firmware entry pinned to the released file,
  `OpenPS2BIOS-0.2.bin` (SHA-1 BCE30600..., the same bytes
  `build-openps2bios.sh` builds). Its description says where to download it,
  that it holds no Sony code, and what it cannot do yet: start a lone
  executable, read a memory card back. It says too that this core was
  tested on it only with its own test program. The default stays the retail
  USA v2.30.
- With no disc, the core picks the field rate from the Boot ROM's region
  letter. It now reads that letter only from a retail value (`ps2-...`) and
  takes NTSC for any other, which is what OpenPS2BIOS is (its ROMVER,
  `0002AD20261017`, says USA) and what the arcade values already got.
- **The frontend test runs on CI.** `run-frontend.sh` falls back to the built
  OpenPS2BIOS and to `tests/own/chimera-padtest.iso` (or takes
  `CHIMERA_PS2_DISC`), and names the Boot ROM by the bios file's SHA-1 as the
  wizard would, not by its file name. The frontend job builds OpenPS2BIOS
  as the core job does.
- **Measured** (this machine): the frontend test on OpenPS2BIOS and the test
  disc, 2 passed (`disc:frontend`, `keybinds`), 2 skipped (`settings:clock`:
  the test disc does not read the clock; `bios:default`: needs the retail
  default); on the retail BIOS and a game, 3 passed, 1 skipped (the game does
  not read the clock in 200 frames either), with the Boot ROM now found by
  SHA-1. The core gate on the retail BIOS: 44 passed, 0 failed, 6 skipped,
  the same checks as before. The package's contract tests: 20 passed.

## 2026-10-19: the test disc reads the clock, so settings:clock runs on it (user-decided)

The owner asked for `settings:clock` to run on CI. It sets the console's
clock to another year and needs the disc to read the clock, so that the
answer lands in the IOP's memory; chimera-padtest did not, so the check said
SKIP. The program now asks the BIOS for the clock once at the start, through
ps2sdk's CDVD library (`sceCdInit`, then `sceCdReadClock`), which OpenPS2BIOS
0.2 serves as a retail BIOS does. Nothing it draws changed. The new ELF and
disc build byte for byte the same each time.

Measured here: the frontend test on OpenPS2BIOS and the disc, 3 passed (with
`settings:clock`, which said SKIP with the old disc) and 1 skipped
(`bios:default`); on the retail BIOS and the disc, 4 passed. The core gate,
the same checks as before: retail BIOS 44 passed, 0 failed, 6 skipped;
OpenPS2BIOS 38 passed, 0 failed, 13 skipped.

## 2026-10-20: a memory card under any name is taken (chimera#328, user-decided)

A card from PCSX2 named anything but Mcd001.ps2 or Mcd002.ps2 (PCSX2 lets a
person name cards, THPS.ps2 in the issue) was refused: the core read only
the names for a slot. The owner decided: such a card fills slot 1 and then
slot 2, in the order the project lists the files; it keeps its name, which
is a label and not a rule; and its SHA-1 is what identifies it, with a
warning when it differs. The last was already true of every project file
(the project's file window says HASH MISMATCH and offers Open Anyway), so
nothing in the core is needed for it.

`PlanSaveData`, first thing in Init, decides which file is which card, and
each card is opened from that file and leaves through Export Save Data under
the same name. That changes one thing from #156: a card brought as
Mcd001.ps2 now leaves as Mcd001.ps2, not memcard1.ps2. A card this run made
leaves as memcardN.ps2, as before. Refused, because the machine would
otherwise ignore them without a word: a third card, a card for a slot whose
Memory Card setting is off, and a file that is neither a card nor bios.nvm.
On an arcade board with its security dongle, slot 1 is the dongle's, and a
card under another name goes to slot 2.

Measured: the core gate on the retail BIOS, 45 passed, 0 failed, 6 skipped
(new: savedata:any-name - THPS.ps2 is the machine the same card as
memcard1.ps2 makes, native == sandbox, it leaves as THPS.ps2, two such cards
fill both slots, and the three refusals).

## The GL names a state holds (chimera#367, 2026-10-24)

Built against miniBox 96762cf and later, whose generated GL wrappers keep
the highest name of each kind the driver has given the guest in the guest's
own memory, so every state holds it. After a load Chimera keeps every number
up to those marks from new objects, which covers a state taken by another
process too, such as a project opened again with its greenzone read back
from disk. Before, a rebuild deleting an object by its old number could
delete one it had just made (Dolphin's vertex arrays drew a grey screen
black). Nothing in this core's own code changed: its build generates the
wrappers from miniBox, so this note is the commit that rebuilds it. A test
runner's host half answers the new opcode with nothing (miniBox b7bc1a2).

## PCSX2's patch collection is in the core, each patch a setting (chimera#371, user-decided 2026-10-25)

The request was a 16:9 picture. Stretching the picture already works in
Chimera (Config > Display > Use custom AR), and is right for a game with its
own 16:9 option. A real widescreen picture needs PCSX2's widescreen patches,
which change the game's code. The owner decided to ship them, and then every
other patch beside them: each patch PCSX2's collection has for a game is a
setting of that game, "Use Custom Patch: Enable <its name>", off unless the
project turns it on.

**Where they come from.** PCSX2/pcsx2_patches, the collection upstream ships
as patches.zip, is the submodule `extern/pcsx2_patches`, pinned (4,719 files
at ba8ef4f4). It states no licence; the owner decided to ship it regardless,
and `waterbox/package-licenses.json` credits it. `waterbox/gen-pnach.py`
compiles the files into a table in the core (3.3 MB of text), as
`gen-gamedb.py` does the game database. Patch 0030 makes upstream's
`EnumeratePnachFiles` read that table under the names it would look for in the
zip (SERIAL_CRC.pnach, then CRC.pnach); still no patch comes from a folder of
files, and there are no cheats. A patch outside a named group would be turned
on by upstream with no way to turn it off; there is none, and the generator
stops on one.

**How a game names its patches.** A patch is per game, so its setting is a
setting of the game's (the way FBNeo's dip switches are): the core says which
there are. Before the machine exists, the new-project wizard asks
`SuggestSettings` (`"suggestSettings": true`), which reads the disc the way
upstream's game list does - SYSTEM.CNF for the serial, the boot program for
the checksum - and answers no values, a note, and the patches as settings.
Once the machine has started, `GetGameSettings` answers the same for the
project's settings (Chimera's File > Project Settings). The setting's name is
`patch:<the patch's name>`; its description is the patch's own, its author,
and that a movie made with it plays back only with it.

**How one is turned on.** Init reads every `patch:` setting that is true from
the mounted settings file and gives the names to upstream's list of enabled
patches (`Patches/Enable`), which `Patch::ReloadEnabledLists` reads. The two
switches upstream offers (widescreen, no-interlacing) stay off; the names do
the work. A patch turned on that the game's patches do not have is refused,
with its name: a movie made with it would play back without it. The settings
reader is given room for a few hundred settings (32 KiB, 2,048 tokens).

**The picture's shape.** A widescreen patch says `gsaspectratio=16:9`, which
upstream applies as its custom aspect ratio. `GetDisplayAspectX/Y` hand that
to Chimera, so the picture is shown at 16:9; with no such patch they answer 0
and the package's own shape stands.

Measured with Black (Europe), SLES-53886, checksum ADDFF505: the wizard is
told of its four patches (Widescreen 16:9, 50/60 FPS, Video Mode, No Blur
While Reload). After 300 frames with the widescreen setting on, its words are
in memory (00000001 at 003BE8BC, 3FE38E39 at 004CAD54; 00000000 and
3FAAAAAB with it off), and chimera-run reports aspect=17778:10000; off, it
reports none. A project turning on "Widescreen 16:10", which Black does not
have, is refused with that name.

Tests: `pnach:table` (no bios), `pnach:refused` (a bios), `pnach:widescreen`
(Black, by `PCSX2_BLACK_DISC`).
