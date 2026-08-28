#!/bin/sh
# The curated PCSX2 source set: what the Chimera core compiles of upstream, and
# nothing else. Both flavors of the build read it from here - the guest
# (core.wbx) and the native reference the equivalence gate compares against -
# so there is one answer to "which sources are this core", not two that drift.
#
# It is a script rather than a list because the set is described by SHAPE:
# "the machine, and the software renderer, and nothing that talks to a person".
# A pin bump that adds a chip should be picked up; one that adds a GPU backend
# or a settings dialog should not.
#
# What is deliberately absent, and why:
#   GS/Renderers/{Vulkan,DX11,DX12,Metal}         no GPU in a sandbox
#   GS/Renderers/{OpenGL,HW}                      built only when this core is
#                                                 given a guest Mesa: then the
#                                                 GL renderer and the OpenGL
#                                                 answering it both run INSIDE
#                                                 the sandbox (gl-osmesa.cpp)
#   x86/                                          the EE, IOP and VU
#                                                 recompilers: this core
#                                                 interprets (docs/PLAN.md)
#   MTVU.cpp                                      a second CPU thread
#   ImGui/, Input/, PAD/, USB/, DEV9/             a frontend already exists,
#   Achievements*, Recording/, Debug*             and a sandbox has no sockets
#   SPU2/ backends, CDVD/ host drives             no audio device, no real drive
#
# Prints paths relative to the repository root, one per line.
#
# Usage: sources.sh [main|deps]
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
p="extern/pcsx2"
cd "$root"

# The machine: the EE and its interpreter, the IOP, the vector units, memory,
# counters, timers, the DMA controllers, the GIF and VIF, the IPU, the CDVD
# front end, and the SPU2 core.
core_srcs() {
	find "$p/pcsx2" -maxdepth 1 -name '*.cpp' | grep -v \
		-e "MTVU.cpp$" \
		-e "BuildVersion.cpp$" \
		-e "ImGui" -e "Achievements" -e "Recording" \
		-e "GameDatabase" -e "GameList" -e "Patch" \
		-e "SysForwardDefs" -e "USB" -e "DEV9" \
		-e "IopModuleNames" \
		-e "SaveState"      # savestates: the sandbox snapshots the whole guest
	find "$p/pcsx2/ps2" "$p/pcsx2/IPU" \( -name '*.cpp' \)
	# The disassembler and the recompilers. Neither RUNS here - this core
	# interprets - but the EE and IOP opcode tables name both implementations
	# of every instruction, so a build without them is a build with five
	# hundred undefined symbols. Compiling what you do not call is cheaper than
	# stubbing it, and it leaves the recompilers one config switch away if the
	# sandbox turns out to be able to host them.
	find "$p/pcsx2/DebugTools" -name '*.cpp' | grep -v -e "Qt"
	# The audio stream: PCSX2's mixer and its null sink. The DEVICE backends
	# (cubeb, SDL) are absent - a frontend takes samples through the ABI.
	find "$p/pcsx2/Host" -name '*.cpp' | grep -v -e "Cubeb" -e "SDL"
	# SIO: the serial IO the controllers and the MEMORY CARDS hang off. A
	# machine without it has no input and nothing to save to.
	# ...minus the one part that faces a PERSON rather than the machine: the
	# folder memory card, which reads a directory of loose files through a yaml
	# index. A memory card here is a file the save-data channel carries.
	find "$p/pcsx2/SIO" -name '*.cpp' | grep -v -e "MemoryCardFolder"
	find "$p/pcsx2/x86" -name '*.cpp'
	# CDVD: the drive and every image reader (iso, chd, cso/zso), but not the
	# HOST's optical drive - a sandbox has no such thing, and a movie cannot
	# cite a disc that was in someone's tray.
	find "$p/pcsx2/CDVD" -name '*.cpp' | grep -v \
		-e "/Windows/" -e "/Darwin/" -e "/Linux/" -e "CDVDdiscReader" \
		-e "CDVDdiscThread"
	# SPU2: the chip. Its output goes through the ABI, not through a device.
	find "$p/pcsx2/SPU2" -name '*.cpp' | grep -v -e "Host" -e "Backend" -e "cubeb"
}

# The GS: the graphics synthesizer itself, plus the SOFTWARE renderer, which is
# the reason this port is possible at all. Common/ is the shared machinery both
# renderers use (texture cache, vertex handling); Null/ is what a GS with
# nothing attached does.
gs_srcs() {
	# ...minus the two files that write IMAGES: a png dump of a texture and the
	# capture path. What this core draws leaves through the ABI as pixels.
	find "$p/pcsx2/GS" -maxdepth 1 -name '*.cpp' | grep -v -e "GSCapture" -e "GSPng"
	# The software renderer INCLUDING its scanline code generators, which this
	# build does not run (patch 0001 puts them behind a switch) but whose
	# function maps are members of GSDrawScanline either way. Compiling what you
	# do not call leaves the JIT one define away, on the day the sandbox turns
	# out to be able to host it.
	find "$p/pcsx2/GS/Renderers/Common" "$p/pcsx2/GS/Renderers/SW" \
		"$p/pcsx2/GS/Renderers/Null" -name '*.cpp' | grep -v -e "arm64"

	# The OpenGL renderer, when there is an OpenGL for it to call. Mesa's
	# softpipe is compiled into the guest, so this is not a GPU: it is a second
	# software renderer, and the one most games are looked at through.
	# The platform contexts (EGL, WGL, X11, Wayland) stay out - there is no
	# window system here, and waterbox/gl-osmesa.cpp is the context instead.
	if [ -n "${CHIMERA_GUEST_MESA:-}" ]; then
		# ...minus the texture replacement loaders, which decode PNG and DDS
		# off disk for a user's own replacement textures. A project's picture
		# is the machine's, and a core has no texture pack directory.
		find "$p/pcsx2/GS/Renderers/HW" -name '*.cpp' \
			| grep -v -e "GSTextureReplacementLoaders"
		find "$p/pcsx2/GS/Renderers/OpenGL" -name '*.cpp' \
			| grep -v -e "GLContextEGL" -e "GLContextWGL"
		echo "$p/3rdparty/glad/src/gl.c"
	fi
}

# common/ is PCSX2's own utility library: files, threads, memory, strings.
common_srcs() {
	# The utility library, minus what talks to the world: no HTTP client, no
	# crash handler, no window system, no yaml (the game database is a
	# frontend's business - a Chimera project states its own settings).
	# the x86 emitter, which the recompilers are written against
	find "$p/common/emitter" -name '*.cpp'
	find "$p/common" -maxdepth 1 -name '*.cpp' | grep -v \
		-e "CocoaTools" -e "D3D" -e "Vulkan" -e "GL/" -e "Windows" \
		-e "HTTPDownloader" -e "StackWalker" -e "CrashHandler" \
		-e "YAML" -e "Zip" -e "Image"
	# ...and Linux's own corner of it, minus the two files that ask the HOST
	# what it is: LnxMisc reads the wall clock and the machine's memory, and
	# DriveUtility enumerates optical drives through udev. waterbox/host-sys.cpp
	# answers the first with a pinned clock; nothing needs the second.
	find "$p/common/Linux" -name '*.cpp' 2>/dev/null | grep -v -e "LnxMisc" -e "DriveUtility" || true
}

deps_srcs() {
	# imgui: the GS draws its on-screen display through it. Nothing here shows
	# an OSD, but the GS is written against it and a core does not fork the GS
	# to avoid a dependency it can simply compile.
	find "$p/3rdparty/imgui/src" -name '*.cpp' | grep -v -e "freetype"
	# The audio stream's own machinery: surround decoding and the time stretcher
	# PCSX2 mixes with. Neither talks to a device.
	find "$p/3rdparty/freesurround/src" -name '*.cpp'
	find "$p/3rdparty/soundtouch/source/SoundTouch" -name '*.cpp' 2>/dev/null || true
	# the demangler ccc uses for C++ symbol names
	find "$p/3rdparty/demangler" -name '*.c' | grep -v -e "testsuite"
	# ccc: PCSX2 reads an ELF's debug symbols with it, and the EE, the IOP bios
	# and the elf loader all reference it.
	find "$p/3rdparty/ccc/src/ccc" -name '*.cpp' 2>/dev/null || true
	# fmt: PCSX2 formats everything through it, including its logs.
	find "$p/3rdparty/fmt/src" -name '*.cc' | grep -v -e "fmt.cc"
	# libchdr and the lzma it needs: CHD is the disc format a project will use.
	find "$p/3rdparty/libchdr/src" -name '*.c'
	find "$p/3rdparty/lzma/src" -name '*.c'
	# The compression libraries PCSX2 takes from the SYSTEM and a sandbox has
	# not: zlib and zstd under CHD, lz4 under CSO. Vendored here (submodules)
	# rather than linked, so the guest and the native reference read the same
	# source and a disc decompresses to the same bytes in both.
	find extern/zlib -maxdepth 1 -name '*.c' | grep -v -e "example" -e "minigzip" -e "infback"
	find extern/zstd/lib/common extern/zstd/lib/compress extern/zstd/lib/decompress \
		-name '*.c'
	echo extern/lz4/lib/lz4.c
	echo extern/lz4/lib/lz4frame.c
	echo extern/lz4/lib/lz4hc.c
	echo extern/lz4/lib/xxhash.c
}

case "${1:-main}" in
	main) { core_srcs; gs_srcs; common_srcs; } | sort ;;
	deps) deps_srcs | sort ;;
	*) echo "usage: sources.sh [main|deps]" >&2; exit 2 ;;
esac
