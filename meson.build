# chimera-core-pcsx2: PCSX2 in miniBox's sandbox, as a Chimera core.
#
#   guest (core.wbx)   sh waterbox/setup-guest.sh
#                      ninja -C build/meson-guest core.wbx
#   native reference   meson setup build/meson-native
#                      ninja -C build/meson-native
#
# Both flavors compile the SAME curated source set (waterbox/sources.sh) with
# the SAME defines, and the equivalence gate compares them frame for frame.
# No GPU backend is built: a sandbox has none, and M1 draws nothing at all
# (see docs/PLAN.md - the renderer is its own milestone).
project('chimera-core-pcsx2', 'c', 'cpp',
  default_options: ['c_std=gnu11', 'cpp_std=c++20',
                    'optimization=2', 'debug=false', 'warning_level=0'])

fc = 'extern/pcsx2'
fc_abs = meson.current_source_dir() / fc
sh = find_program('sh')

# the submodule pin is pristine upstream; overlay the chimera patch set
# (idempotent - see docs/PLAN.md)
run_command(files('waterbox/apply-patches.sh'), check: true)

mb = get_option('minibox_dir')
if mb == ''
  mb_candidate = meson.current_source_dir() / '../chimera/extern/tools/chimera-common-minibox'
  if import('fs').is_dir(mb_candidate)
    mb = mb_candidate
  else
    error('pass -Dminibox_dir=<miniBox checkout> (or keep one at ../chimera/extern/tools/chimera-common-minibox)')
  endif
endif

# CHIMERA_WATERBOX marks the places upstream asks the host for something a
# sandbox does not have. NO_ASAN/NDEBUG are upstream's own; LIBRETRO is NOT
# set: this core drives Emulator directly rather than through the libretro
# shell, which exists to serve a GUI this core does not have.
defs = [
  '-DNDEBUG',
  '-DPCSX2_CORE',
  '-DCHIMERA_WATERBOX',
  # The software renderer generates its scanline routines at runtime through
  # xbyak. A sandbox would rather not host a code generator, and does not have
  # to: GSDrawScanline::CDrawScanline is the C++ path this build takes.
  '-DCHIMERA_NO_JIT_RASTERIZER', '-DCHIMERA_SOFTWARE_ONLY', '-DCHIMERA_NO_INPUT_SOURCES', '-DCHIMERA_NO_GS_THREAD', '-DCHIMERA_NO_SHARED_MEMORY', '-DCHIMERA_FLAT_FILESYSTEM', '-DCHIMERA_MEMCARD_IN_MEMORY', '-DCHIMERA_NVRAM_IN_MEMORY', '-DCHIMERA_LAG_DETECTION',
  # zstd ships hand-written amd64 assembly for one decode loop. The C path is
  # the same bytes out and one source language fewer in a cross-built guest.
  '-DZSTD_DISABLE_ASM',
  # What both flavors agree on, whichever C library is underneath.
  '-include', 'chimera-shim.h',
  '-Wno-deprecated-declarations',
  '-Wno-unused-variable',
  # PCSX2 refuses to compile below SSE 4.1, and the emulation itself uses those
  # instructions: both flavors get the same ones, so both compute the same way.
  '-msse4.1',
  # What PCSX2's build system sets to say which machine it is compiling FOR.
  # Without it the processor-feature struct loses its members and the emitter's
  # headers stop making sense.
  '-D_M_X86',
  # What PCSX2's build system passes the GNU demangler, whose headers otherwise
  # redeclare basename() and collide with the C library's.
  '-DHAVE_DECL_BASENAME=1', '-DHAVE_LIMITS_H=1', '-DHAVE_STDLIB_H=1',
  '-DHAVE_STRING_H=1',

]
# musl carries neither of these glibc extensions, and glibc has both - so this
# is the one place the two flavors are compiled differently, and it is about
# the C LIBRARY rather than about the machine. gettid becomes the sandbox's own
# answer (one thread, id 1); the two sysconf queries become -1, which is "do
# not know", and PCSX2 already handles that by reading /sys, finding nothing,
# and reporting a cache line size of zero - the same answer glibc's sysconf
# gives inside the sandbox, where there is no /sys either.
if meson.is_cross_build()
  defs += [
    '-Dgettid=chimera_gettid',
    '-D_SC_LEVEL1_DCACHE_LINESIZE=-1',
    '-D_SC_LEVEL1_ICACHE_LINESIZE=-1',
  ]
endif

add_project_arguments(defs, language: ['c', 'cpp'])

inc = include_directories(
  fc,
  fc / 'pcsx2',
  fc / 'common',
  fc / '3rdparty/fmt/include',
  fc / '3rdparty/libchdr/include',
  fc / '3rdparty/lzma/include',
  fc / '3rdparty/fast_float/include',
  fc / '3rdparty/rapidjson/include',
  fc / '3rdparty/simpleini/include',
  fc / '3rdparty/imgui/include',
  fc / '3rdparty/ccc/src',
  fc / '3rdparty/demangler/include',
  fc / '3rdparty/freesurround/include',
  'extern/zlib',
  'extern/zstd/lib',
  fc / '3rdparty/soundtouch',
  fc / '3rdparty/soundtouch/soundtouch',
  fc / '3rdparty/xbyak',
  fc / '3rdparty/include',
  fc / '3rdparty/cpuinfo/include',
  fc / '3rdparty/zydis/include',
  # lz4 comes last: it carries a private xxhash.h, and PCSX2's own copy (in
  # 3rdparty/include) is the one GSXXH.cpp needs the internals of.
  'extern/lz4/lib',
  'waterbox',
  'waterbox/generated',
)

sources_sh = files('waterbox/sources.sh')
core_sources = files(run_command(sources_sh, 'main', check: true).stdout().strip().split('\n'))
dep_sources = files(run_command(sources_sh, 'deps', check: true).stdout().strip().split('\n'))

deps_lib = static_library('pcsx2-deps', dep_sources, include_directories: inc)


# What a sandbox answers differently from a desktop. Each file says what it
# refuses and why; none of them patch upstream.
driver_sources = files(
  'waterbox/cinterface.cpp',
  'waterbox/host.cpp',
  'waterbox/host-sys.cpp',
  'waterbox/gs-device.cpp',
  'waterbox/audio-stream.cpp',
  'waterbox/stubs/frontend.cpp',
  'waterbox/stubs/peripherals.cpp',
  'waterbox/stubs/refused.cpp',
  'waterbox/stubs/host-stubs.cpp',
  'waterbox/stubs/posix-stubs.cpp',
)

if meson.is_cross_build()
  # ---- the guest: core.wbx --------------------------------------------------
  mbdir = meson.get_external_property('minibox_dir')
  mbuild = meson.get_external_property('minibox_build')
  sr = meson.get_external_property('guest_sysroot')

  executable('core', core_sources + driver_sources,
    name_suffix: 'wbx',
    include_directories: inc,
    link_with: [deps_lib],
    link_args: [
      '-static', '-no-pie',
      '-Wl,--eh-frame-hdr', '-Wl,-O2', '-Wl,--no-relax',
      '-Wl,-z,stack-size=8388608',
      '-T', mbdir / 'source/guest/linkscript.T',
      '-Wl,-u,pthread_once', '-Wl,-u,pthread_cond_wait',
      '-Wl,-u,pthread_cond_broadcast', '-Wl,-u,pthread_key_create',
      mbuild / 'source/guest/cxxglue.c.o',
      mbuild / 'source/guest/emulibc.c.o',
      '-L' + sr / 'lib', '-lstdc++', '-lgcc', '-lgcc_eh', '-lc'])
else
  # ---- the native reference and the sandbox driver --------------------------
  threads = dependency('threads')
  dl = meson.get_compiler('c').find_library('dl', required: false)
  m = meson.get_compiler('c').find_library('m', required: false)

  executable('run-native', core_sources + driver_sources + files('waterbox/run-native.c'),
    include_directories: [include_directories('waterbox/native-shim'), inc],
    c_args: ['-I' + mb / 'source/guest/include', '-I' + mb / 'extern/jsmn'],
    cpp_args: ['-I' + mb / 'source/guest/include', '-I' + mb / 'extern/jsmn'],
    link_with: [deps_lib],
    dependencies: [threads, dl, m])

  mbhost = mb / 'build/meson-cpp/source/host'
  # The sandbox runner is not part of the machine: it drives core.wbx from
  # outside, so it wants neither the machine's includes nor its shim.
  executable('run-wbx', files('waterbox/run-wbx.c'),
    c_args: ['-I' + mb / 'source/host', '-I' + meson.current_source_dir() / 'waterbox/generated'],
    link_args: [mbhost / 'libminiboxhost.so', '-Wl,-rpath,' + mbhost])
endif
