# chimera-readbacktest

A PlayStation 2 test program of the PCSX2 core's, on a disc of its own
(`../chimera-readbacktest.iso`). It is this repository's, free to copy (see
`LICENSE`), so the test that needs it can run anywhere, CI included.

Every frame it clears the picture, draws a square whose colour changes with
the frame count and a line that moves a pixel a frame, and copies the square
within the GS's memory as 16-bit pixels. A graphics card cannot change a
pixel's size in a copy, so PCSX2's hardware renderers read the square back
from the card for it, through their texture cache's download texture. The
gate's `vk:readback` check runs it with a state saved and loaded around every
frame: after each load the readback must go to the new Vulkan device
(chimera issue 560). It uses nothing but the GS, so it runs on a retail BIOS
and on OpenPS2BIOS alike.

Build: `sh build.sh <ps2dev root>` (the ps2dev/ps2dev v2.0.0 image unpacked;
the compiler runs chrooted into it in a user namespace) or with Docker, as
`build.sh` says. Both outputs come out byte for byte the same each time.
