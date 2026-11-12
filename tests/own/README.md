Content this repository may legally ship, and therefore does track. The sibling
`tests/roms/` is ignored wholesale (see the .gitignore, which explains why): a
PlayStation 2 needs a bios dump and the discs are somebody's, so nothing there
can be committed and no per-file rule is trusted to keep it that way. Anything
free to distribute goes here instead, where tracking it needs no exception.

- `chimera-padtest.iso` and `chimera-padtest.elf` - this repository's own
  test program, on a disc of its own and as a bare executable: a cell per
  button for both ports, lit while held, and a bar that grows a pixel a
  frame, drawn once and then only where it changes; it reads the console's
  clock once at the start. Source, build script,
  cell reader and licences (MIT for the program; AFL-2.0 for the ps2sdk and
  gsKit code it is linked with; newlib's) are in `chimera-padtest/`. It reads
  the pads on a retail BIOS and on OpenPS2BIOS alike, and is what the gate's
  pad, gun, column and renderer checks run, and the disc the frontend test
  runs when there is no game.
- `chimera-fputest-full.iso` and `chimera-fputest-full-round-up.iso` - this
  repository's test of the EE's floating point: every FPU instruction on
  634,880 sets of operands, the results kept in memory and nothing drawn.
  The two discs hold the same program under the names of two games the
  game database puts in the FPU's Full clamp mode (one also rounds up), so
  that PCSX2 turns that mode on; nothing of either game is on them. Source,
  build script, the comparison script and licences (MIT for the program;
  AFL-2.0 for the ps2sdk code it is linked with; newlib's) are in
  `chimera-fputest/`. It runs on a retail BIOS and on OpenPS2BIOS alike, and
  is what the gate's `fpu:full-mode` check runs.
- `chimera-readbacktest.iso` - this repository's test of what a game reads
  back of what it drew: every frame it draws a square and copies it within
  the GS's memory as 16-bit pixels, which the hardware renderers do by
  reading the square back from the graphics card. Source, build script and
  licences (MIT for the program; AFL-2.0 for the ps2sdk and gsKit code it is
  linked with; newlib's) are in `chimera-readbacktest/`. It runs on a retail
  BIOS and on OpenPS2BIOS alike, and is what the gate's `vk:readback` check
  runs (chimera issue 560).
