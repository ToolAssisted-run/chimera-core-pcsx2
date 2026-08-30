#!/bin/bash
# The core-level equivalence gate: the sandboxed core must produce
# byte-identical video, audio, lag and memory-domain digests to the native
# reference build (the same sources compiled natively), and must survive a
# whole-machine savestate round-trip around every frame.
#
# SCOPE, AND WHY THIS GATE IS SHAPED DIFFERENTLY FROM THE OTHERS.
#
# Every other core here can prove itself from an empty checkout, because it can
# run a program this repository builds. A PlayStation 2 cannot: it has no HLE
# bios, nothing executes until a real one is present, and a bios is somebody's
# dump. So this gate is in two tiers.
#
# The first tier needs nothing and always runs. It is small, but it is not
# nothing: a core that cannot find a bios must say so and stop, in both
# flavors, rather than crash or pretend - and that path is the one every user
# without a dump will meet first.
#
# The second tier needs a bios (tests/roms/bios/*.bin, or $CHIMERA_PS2_BIOS),
# and a third needs a disc (tests/roms/*.bin) on top of that. Both are the
# user's own content and neither is in this repository. When they are absent
# the checks report SKIP and say what they would have proven, so that a green
# run never quietly means "nothing was tested".
#
# Usage: ./run-gate.sh [-n <native build dir>] [-g <guest build dir>]
set -u

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
nat="$root/build/meson-native"
gst="$root/build/meson-guest"
while getopts "n:g:" opt; do
	case "$opt" in
		n) nat="$OPTARG" ;;
		g) gst="$OPTARG" ;;
		*) exit 2 ;;
	esac
done

[ -x "$nat/run-native" ] && [ -x "$nat/run-wbx" ] || {
	echo "native build missing: meson setup build/meson-native && ninja -C build/meson-native" >&2; exit 1; }
[ -f "$gst/core.wbx" ] || {
	echo "guest build missing: sh waterbox/setup-guest.sh && ninja -C build/meson-guest core.wbx" >&2; exit 1; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
digests() { grep -E '^(frames|vsync|videoHash|audioHash|lagFrames|domain\[)'; }
# What a turbo run can be held to: everything except the whole-run video hash,
# which a run that skipped the first half cannot possibly match - the second
# half it did draw is compared instead.
turboDigests() { grep -E '^(frames|vsync|tailVideoHash|audioHash|lagFrames|domain\[)'; }

ok=0
failed=0
skipped=0
report() {
	printf "%-28s %-6s %s\n" "$1" "$2" "$3"
	case "$2" in
		PASS) ok=$((ok + 1)) ;;
		SKIP) skipped=$((skipped + 1)) ;;
		*) failed=$((failed + 1)) ;;
	esac
}
printf "%-28s %-6s %s\n" "Check" "Result" "Detail"
printf "%-28s %-6s %s\n" "-----" "------" "------"

# ---- tier one: no content needed -------------------------------------------
# A machine with no bios is the state every user without a dump starts in. It
# must be a clear refusal rather than a crash, and it must be the same refusal
# in the sandbox as natively.
empty="$work/empty"
mkdir -p "$empty"
printf '{}' > "$empty/settings"

nat_out="$("$nat/run-native" "$empty" --frames 1 2>&1)"
nat_rc=$?
box_out="$("$nat/run-wbx" "$gst/core.wbx" "$empty" --frames 1 2>&1)"
box_rc=$?

if [ "$nat_rc" -eq 0 ] || [ "$box_rc" -eq 0 ]; then
	report "nobios:refuses" FAIL "a machine with no bios reported success"
elif ! grep -qi "bios" <<< "$nat_out" || ! grep -qi "bios" <<< "$box_out"; then
	report "nobios:refuses" FAIL "the refusal does not mention the bios"
else
	report "nobios:refuses" PASS "both flavors refuse, and say why"
fi

# ---- what content there is -------------------------------------------------
bios="${CHIMERA_PS2_BIOS:-}"
if [ -z "$bios" ]; then
	bios="$(find "$root/tests/roms/bios" -maxdepth 1 -name '*.bin' 2>/dev/null | sort | head -1)"
fi
disc="$(find "$root/tests/roms" -maxdepth 1 \( -name '*.bin' -o -name '*.iso' \) 2>/dev/null | sort | head -1)"

if [ -z "$bios" ]; then
	report "bios:equivalence" SKIP "no bios: put one in tests/roms/bios/ or set CHIMERA_PS2_BIOS"
	report "bios:ran" SKIP "would prove the EE executed"
	report "bios:savestate" SKIP "would prove a per-frame state round-trip is lossless"
	report "bios:turbo" SKIP "would prove an undrawn frame leaves the same machine"
	report "video:deinterlace" SKIP "would prove the deinterlacer follows the machine's display mode"
	report "video:rasterisers" SKIP "would prove the C++ rasteriser draws what the code generator draws"
	report "native:determinism" SKIP "would prove the native reference does not wander"
	report "domains" SKIP "would prove all five memory domains are exposed"
	report "video:drew" SKIP "would prove the software renderer draws"
	report "video:steady" SKIP "would prove the two fields are put back together"
	report "input:shaped" SKIP "would prove the machine reads its pad"
	report "input:lag" SKIP "would prove lag frames are counted"
	report "savedata:exports" SKIP "would prove the cards and NVRAM leave through the channel"
	report "savedata:roundtrip" SKIP "would prove what is mounted comes back"
	report "savedata:multitap" SKIP "would prove a multitap reaches six more card slots"
	report "disc:boots" SKIP "needs a bios as well as a disc"
	report "disc:equivalence" SKIP "needs a bios as well as a disc"
	echo
	echo "$ok ok, $failed failed, $skipped skipped"
	[ "$failed" -eq 0 ]
	exit $?
fi

# ---- tier two: the machine, with a bios ------------------------------------
wd="$work/bios"
mkdir -p "$wd"
cp "$bios" "$wd/bios.bin"
printf '{}' > "$wd/settings"

FRAMES=300

if ! "$nat/run-native" "$wd" --frames "$FRAMES" 2>"$work/nat.err" | digests > "$work/nat.txt"; then
	report "bios:equivalence" FAIL "native runner error: $(head -1 "$work/nat.err")"
elif ! "$nat/run-wbx" "$gst/core.wbx" "$wd" --frames "$FRAMES" 2>"$work/box.err" | digests > "$work/box.txt"; then
	report "bios:equivalence" FAIL "waterbox runner error: $(head -1 "$work/box.err")"
elif cmp -s "$work/nat.txt" "$work/box.txt"; then
	report "bios:equivalence" PASS "$FRAMES frames of $(basename "$bios"), native == waterboxed"
else
	report "bios:equivalence" FAIL "$(diff "$work/nat.txt" "$work/box.txt" | tr '\n' ' ' | head -c 120)"
fi

# Turbo: the display stage switched off for the first half of the run and back
# on for the second. Everything the EE can see - and every picture of that
# second half - must be what it would have been.
#
# --turbo-settle 3 excuses exactly THREE pictures, and the deinterlacer earns
# them: the motion-adaptive one remembers four fields across two banks, so the
# frames that resume drawing after a gap are reconstructed from a history that
# is not there yet. Three is measured, not assumed - two still differ and three
# matches - and it is the same artifact a savestate load produces. Both runs
# skip the same frames, so nothing else is excused.
if ! "$nat/run-wbx" "$gst/core.wbx" "$wd" --frames "$FRAMES" --turbo-settle 3 2>/dev/null | turboDigests > "$work/tnorm.txt"; then
	report "bios:turbo" FAIL "drawn run failed"
elif ! "$nat/run-wbx" "$gst/core.wbx" "$wd" --frames "$FRAMES" --turbo --turbo-settle 3 2>/dev/null | turboDigests > "$work/turbo.txt"; then
	report "bios:turbo" FAIL "turbo run failed"
elif cmp -s "$work/tnorm.txt" "$work/turbo.txt"; then
	report "bios:turbo" PASS "$FRAMES frames, half of them undrawn, same machine and same pictures"
else
	report "bios:turbo" FAIL "$(diff "$work/tnorm.txt" "$work/turbo.txt" | tr '\n' ' ' | head -c 120)"
fi

# THE TWO RASTERISERS MUST AGREE. PCSX2 draws the software renderer's scanlines
# with a code generator; a sandbox cannot host one, so the shipped core takes
# the C++ fallback instead - a path far fewer people run, and one that had a
# real bug in it (negative colour deltas saturated instead of truncating, which
# shredded every Gouraud-shaded surface into four-pixel bands). Nothing in this
# gate could see it, because both flavors took the same wrong path.
#
# So: if a reference built with -Djit_rasterizer=true is present, its digests
# must equal the ordinary build's, to the byte. It is a native-only comparison
# because the code generator only runs outside the sandbox.
jitnat="${CHIMERA_PS2_JIT_BUILD:-$root/build/meson-jit}"
if [ -x "$jitnat/run-native" ]; then
	if ! "$nat/run-native" "$wd" --frames "$FRAMES" 2>/dev/null | digests > "$work/ras.cpp.txt"; then
		report "video:rasterisers" FAIL "the C++ rasteriser run failed"
	elif ! "$jitnat/run-native" "$wd" --frames "$FRAMES" 2>/dev/null | digests > "$work/ras.jit.txt"; then
		report "video:rasterisers" FAIL "the code-generator run failed"
	elif cmp -s "$work/ras.cpp.txt" "$work/ras.jit.txt"; then
		report "video:rasterisers" PASS "$FRAMES frames, the C++ path draws what the code generator draws"
	else
		report "video:rasterisers" FAIL "$(diff "$work/ras.cpp.txt" "$work/ras.jit.txt" | tr '\n' ' ' | head -c 120)"
	fi
else
	report "video:rasterisers" SKIP "no -Djit_rasterizer=true build to compare against"
fi

# The deinterlacer must follow the MACHINE, not a fixed choice. This is the leg
# that would have caught a core weaving every game: with "auto" (the default),
# a console reporting FFMD=1 - half-height fields - must reach the adaptive
# deinterlacer (mode 3), and a console drawing whole frames must reach no
# deinterlacer at all (mode -1). Only the first case is testable on content that
# is free to distribute; the trace prints both.
modes="$(CHIMERA_GS_TRACE=1 "$nat/run-native" "$wd" --frames "$FRAMES" 2>&1 | sed -n 's/^merge: .*FFMD=\([0-9]*\).* -> mode=\(-\?[0-9]*\).*/\1:\2/p' | sort -u)"
if [ -z "$modes" ]; then
	report "video:deinterlace" FAIL "the machine never reported a display mode"
elif [ "$modes" = "1:3" ]; then
	report "video:deinterlace" PASS "FFMD=1 reaches the adaptive deinterlacer, and nothing else does"
else
	report "video:deinterlace" FAIL "FFMD:mode pairs were $(echo "$modes" | tr '\n' ' ')"
fi

# A hollow pass cannot sneak through: the machine must actually have EXECUTED
# something, so a shorter run must reach a different state than a longer one.
if ! "$nat/run-native" "$wd" --frames $((FRAMES / 2)) 2>/dev/null | digests > "$work/half.txt"; then
	report "bios:ran" FAIL "half-length native run failed"
elif cmp -s "$work/nat.txt" "$work/half.txt"; then
	report "bios:ran" FAIL "half as many frames left the machine in the same state"
else
	report "bios:ran" PASS "the EE executed: $FRAMES frames differ from $((FRAMES / 2))"
fi

# The sandbox snapshots the whole guest, so a savestate here is the whole
# machine by construction; what this checks is that taking one every frame and
# restoring it changes nothing.
if ! "$nat/run-wbx" "$gst/core.wbx" "$wd" --frames 60 2>/dev/null | digests > "$work/rr60.txt"; then
	report "bios:savestate" FAIL "plain 60-frame run failed"
elif ! "$nat/run-wbx" "$gst/core.wbx" "$wd" --frames 60 --rerecord 2>/dev/null | digests > "$work/rr.txt"; then
	report "bios:savestate" FAIL "rerecord run failed"
elif cmp -s "$work/rr60.txt" "$work/rr.txt"; then
	report "bios:savestate" PASS "per-frame round-trip is lossless"
else
	report "bios:savestate" FAIL "$(diff "$work/rr60.txt" "$work/rr.txt" | tr '\n' ' ' | head -c 120)"
fi

# The Stella lesson: the native reference is the only place a real clock and
# real threads still tick, so it is where nondeterminism shows up. Two runs of
# the same machine must agree - and this core has already been caught once, by
# a clock that advanced when it was READ rather than when the machine ran.
a="$("$nat/run-native" "$wd" --frames 120 2>/dev/null | digests)"
b="$("$nat/run-native" "$wd" --frames 120 2>/dev/null | digests)"
if [ "$a" = "$b" ]; then
	report "native:determinism" PASS "two native runs agree"
else
	report "native:determinism" FAIL "the native reference wanders between runs"
fi

# Every domain a movie's watch window needs, present and hashed: the EE's 32MB,
# the IOP's 2MB, the scratchpad the EE works in, and both vector units.
doms="$(grep -c '^domain\[' "$work/nat.txt")"
if [ "$doms" = "5" ]; then
	report "domains" PASS "EE RAM, IOP RAM, scratchpad, VU0 and VU1 exposed"
else
	report "domains" FAIL "$doms domains, want 5"
fi

# ---- the picture -----------------------------------------------------------
# A renderer that draws nothing passes an equivalence test perfectly, so the
# frame the bios reaches must differ from the blank one it starts with - and
# the sandbox must have drawn the same picture.
blank="$("$nat/run-native" "$wd" --frames 2 2>/dev/null | sed -n 's/^videoHash=//p')"
drawn="$(sed -n 's/^videoHash=//p' "$work/nat.txt")"
boxdrawn="$(sed -n 's/^videoHash=//p' "$work/box.txt")"
if [ -z "$drawn" ]; then
	report "video:drew" FAIL "no video digest"
elif [ "$blank" = "$drawn" ]; then
	report "video:drew" FAIL "frame $FRAMES hashes the same as a blank frame"
elif [ "$drawn" != "$boxdrawn" ]; then
	report "video:drew" FAIL "native and sandbox drew different pictures"
else
	report "video:drew" PASS "the software renderer drew, and the sandbox drew the same"
fi

# ---- the picture holds still ------------------------------------------------
# A PS2 in an interlaced mode hands over one FIELD per frame: half an image,
# the other half arriving next time. Put the halves back where they belong and
# a slow scene barely changes between frames; hand each field over stretched
# instead - which is what this core used to do - and the whole picture climbs
# two scanlines and falls back, every frame, forever.
#
# No hash can see that: every frame differs from the last either way. So the
# question is asked in pixels, and asked RELATIVELY - the same 50 frames, woven
# and not woven - because the number itself belongs to whichever bios and
# animation happen to be here.
motion() {
	local mode="$1" dir="$work/motion.$1"
	rm -rf "$dir"; mkdir -p "$dir"
	printf '{"deinterlace":"%s"}' "$mode" > "$wd/settings"
	CHIMERA_SHOT_DIR="$dir" CHIMERA_SHOT_FROM=150 		"$nat/run-native" "$wd" --frames 200 >/dev/null 2>&1
	python3 "$here/tests/frame-motion.py" "$dir" 2>/dev/null
}
woven="$(motion weave)"
plain="$(motion off)"
printf '{}' > "$wd/settings"
if [ -z "$woven" ] || [ -z "$plain" ]; then
	report "video:steady" FAIL "could not measure frame-to-frame motion"
elif ! awk -v a="$woven" -v b="$plain" 'BEGIN { exit !(b > a * 3) }'; then
	report "video:steady" FAIL "woven moves $woven per frame, unwoven $plain - the fields are not being put back"
else
	report "video:steady" PASS "woven $woven per frame against $plain unwoven: the fields go back where they belong"
fi

# ---- input -----------------------------------------------------------------
# The distinction that matters is between "the frontend set a variable" and
# "the machine read its controller". Holding buttons must change the machine,
# and must change it the same way in both flavors.
idle="$("$nat/run-native" "$wd" --frames 200 2>/dev/null | digests)"
held="$("$nat/run-native" "$wd" --frames 200 --exercise 2>/dev/null | digests)"
boxheld="$("$nat/run-wbx" "$gst/core.wbx" "$wd" --frames 200 --exercise 2>/dev/null | digests)"
if [ "$idle" = "$held" ]; then
	report "input:shaped" FAIL "input made no difference to the machine"
elif [ "$held" != "$boxheld" ]; then
	report "input:shaped" FAIL "native and sandbox disagree with input held"
else
	report "input:shaped" PASS "the pad was read: idle != held, native == waterboxed"
fi

# Lag detection (patch 0011): a lag frame is a frame the machine never looked
# at its input. Three things must hold, and none of them depends on which bios
# is in front of it: early frames ARE lag frames (a PS2 does not look at all
# until the IOP has loaded its pad driver), later frames are not all lag, and -
# the precise one - once the machine is polling, the count STOPS GROWING. A
# detector wired to the wrong place tends to fail that last one, by counting
# every frame or none.
lag_early="$("$nat/run-native" "$wd" --frames 30 2>/dev/null | sed -n 's/^lagFrames=//p')"
lag_late="$(sed -n 's/^lagFrames=//p' "$work/nat.txt")"
lag_later="$("$nat/run-native" "$wd" --frames $((FRAMES * 2)) 2>/dev/null | sed -n 's/^lagFrames=//p')"
if [ -z "$lag_early" ] || [ "$lag_early" -lt 20 ]; then
	report "input:lag" FAIL "a machine whose pad driver has not loaded reported $lag_early lag frames of 30"
elif [ -z "$lag_late" ] || [ "$lag_late" -ge "$FRAMES" ]; then
	report "input:lag" FAIL "no frame ever polled the pad ($lag_late of $FRAMES)"
elif [ "$lag_later" != "$lag_late" ]; then
	report "input:lag" FAIL "lag kept accruing after the driver loaded ($lag_late at $FRAMES, $lag_later at $((FRAMES * 2)))"
else
	report "input:lag" PASS "$lag_early of the first 30 frames are lag, and it stops at $lag_late"
fi

# ---- save data -------------------------------------------------------------
# A memory card is 8MB of card plus its ECC, and the console's own memory is a
# kilobyte. Both must leave through the save-data channel, because a sandboxed
# core has nowhere else to put them.
sd="$work/savedata"
mkdir -p "$sd"
"$nat/run-native" "$wd" --frames 60 --savedata-out "$sd" >/dev/null 2>&1
if python3 - "$sd" <<'PYSD'
import sys, os
d = sys.argv[1]
card = os.path.join(d, "memcard1.ps2")
nvm = os.path.join(d, "bios.nvm")
if not os.path.exists(card) or not os.path.exists(nvm):
    print("missing:", sorted(os.listdir(d)))
    sys.exit(1)
# 8MB of card, as the console counts it: 8 * 1024 * 528 * 2, which is the data
# plus the ECC every page carries.
size = os.path.getsize(card)
if size != 8 * 1024 * 528 * 2:
    print("card is", size, "bytes")
    sys.exit(1)
# A card nobody has formatted is erased flash, which is all ones.
if set(open(card, "rb").read(1 << 16)) != {0xFF}:
    print("a fresh card is not blank")
    sys.exit(1)
if os.path.getsize(nvm) != 1024:
    print("nvram is", os.path.getsize(nvm), "bytes")
    sys.exit(1)
sys.exit(0)
PYSD
then
	report "savedata:exports" PASS "a blank 8MB card and 1KB of NVRAM left through the channel"
else
	report "savedata:exports" FAIL "the save-data channel did not carry what it should"
fi

# What the frontend mounts must be what the machine sees, and what comes back
# must be what the machine left. A card the frontend hands over is loaded (the
# machine's digests change), and an untouched one returns unchanged.
mounted="$work/mounted"
mkdir -p "$mounted"
cp "$bios" "$mounted/bios.bin"
printf '{}' > "$mounted/settings"
python3 - "$mounted/memcard1.ps2" <<'PYCARD'
import sys
# a card with something on it: the format signature the console writes, then
# erased flash. Nothing here claims to be a valid filesystem - the point is
# that these exact bytes are what the machine is handed, and what comes back.
size = 8 * 1024 * 528 * 2
data = bytearray(b"\xff" * size)
data[0:32] = b"Sony PS2 Memory Card Format 1.2."
open(sys.argv[1], "wb").write(bytes(data))
PYCARD
sdm="$work/savedata-mounted"
mkdir -p "$sdm"
without="$("$nat/run-native" "$wd" --frames 200 2>/dev/null | digests)"
with_card="$("$nat/run-native" "$mounted" --frames 200 --savedata-out "$sdm" 2>/dev/null | digests)"
box_with="$("$nat/run-wbx" "$gst/core.wbx" "$mounted" --frames 200 2>/dev/null | digests)"
if [ "$without" = "$with_card" ]; then
	report "savedata:roundtrip" FAIL "the machine saw no difference between a blank card and a written one"
elif [ "$with_card" != "$box_with" ]; then
	report "savedata:roundtrip" FAIL "native and sandbox disagree with a card mounted"
elif ! cmp -s "$mounted/memcard1.ps2" "$sdm/memcard1.ps2"; then
	report "savedata:roundtrip" FAIL "the card that came back is not the card that went in"
else
	report "savedata:roundtrip" PASS "mounted, seen by the machine, and returned unchanged"
fi

# ---- eight cards behind two multitaps --------------------------------------
# A multitap turns one socket into four, for memory cards as well as pads, and
# the six extra card slots are reachable ONLY with one plugged in. So this asks
# for all eight and expects all eight - and then asks with the multitaps off and
# expects the console's own two, which is what says the multitap setting is
# doing something rather than the cards being on regardless.
sd8="$work/savedata8"
allcards='"memcard1":true,"memcard2":true,"memcard3":true,"memcard4":true,
"memcard5":true,"memcard6":true,"memcard7":true,"memcard8":true'
cardnames() { # <settings json> -> the exported card files, sorted, space separated
	rm -rf "$sd8"; mkdir -p "$sd8"
	printf '%s' "$1" > "$wd/settings"
	"$nat/run-native" "$wd" --frames 60 --savedata-out "$sd8" >/dev/null 2>&1
	ls "$sd8" 2>/dev/null | grep '^memcard' | sort | tr '\n' ' ' | sed 's/ $//'
}
eight="$(cardnames "{\"multitap1\":true,\"multitap2\":true,$allcards}")"
two="$(cardnames "{$allcards}")"
printf '{}' > "$wd/settings"
want8="memcard1.ps2 memcard2.ps2 memcard3.ps2 memcard4.ps2 memcard5.ps2 memcard6.ps2 memcard7.ps2 memcard8.ps2"
if [ "$eight" != "$want8" ]; then
	report "savedata:multitap" FAIL "with two multitaps the machine exported [$eight]"
elif [ "$two" != "memcard1.ps2 memcard2.ps2" ]; then
	report "savedata:multitap" FAIL "with no multitap the machine exported [$two], want the console's own two"
else
	report "savedata:multitap" PASS "eight cards behind two multitaps, and two without them"
fi

# ---- the pad, as the machine sees it ----------------------------------------
# tests/own/padtest.elf draws every button of a DualShock 2 with the pressure
# the machine is reading. Hold the button a package DECLARES as "Circle" and the
# circle cell must be the one that moves - nothing else can answer that, because
# every layer between a frontend column and the pad is keyed by the same names
# and a swap in one of them is invisible from either end.
#
# It also exercises booting a PS2 EXECUTABLE, which is the other thing this file
# is: no disc in the tray, the program handed to the machine directly.
padelf="$root/tests/own/padtest.elf"
if [ ! -f "$padelf" ]; then
	report "pad:mapping" SKIP "tests/own/padtest.elf is missing"
	report "pad:ports" SKIP "tests/own/padtest.elf is missing"
elif [ -z "$bios" ]; then
	report "pad:mapping" SKIP "needs a bios"
	report "pad:ports" SKIP "needs a bios"
else
	pd="$work/pad"
	mkdir -p "$pd"
	cp "$bios" "$pd/bios.bin"
	cp "$padelf" "$pd/padtest.elf"
	printf '{"disc":["padtest.elf"]}' > "$pd/slots"
	printf '{}' > "$pd/settings"

	# The frames are all EVEN: an interlaced machine hands over half a picture
	# per frame, so two frames an odd number apart differ everywhere.
	if ! "$nat/run-native" "$pd" --frames 400 --screenshot "$work/pad.idle.tga" >/dev/null 2>&1 \
		|| [ ! -s "$work/pad.idle.tga" ]; then
		report "pad:elf" FAIL "the machine did not boot the executable"
	else
		report "pad:elf" PASS "padtest.elf booted with no disc in the tray"

		names="Up Down Left Right Start Select Square Cross Circle Triangle L1 R1 L2 R2 L3 R3"
		wrong=""
		idx=0
		for want in $names; do
			# the press must still be HELD on the frame that is captured, which
			# is the last one - a released button reads as idle and every
			# button would come back as "nothing"
			"$nat/run-native" "$pd" --frames 500 --press 400:100:$idx \
				--screenshot "$work/pad.$idx.tga" >/dev/null 2>&1
			got="$(python3 "$here/tests/pad-cells.py" "$work/pad.idle.tga" "$work/pad.$idx.tga" | tr '\n' ' ' | sed 's/ $//')"
			[ "$got" = "$want" ] || wrong="$wrong $want->[${got:-nothing}]"
			idx=$((idx + 1))
		done
		if [ -z "$wrong" ]; then
			report "pad:mapping" PASS "all 16 buttons light their own readout, and no other"
		else
			report "pad:mapping" FAIL "$wrong"
		fi

		# ---- the second port ------------------------------------------------
		# padtest draws a block per port, so two pads and two DIFFERENT buttons
		# held at once answer both halves of the question in one run: each
		# port's input reached that port, and neither reached the other. A
		# single run rather than sixteen because a PS2 interprets everything and
		# five hundred frames is not cheap.
		#
		# P2's block starts at wire 37, not 17: the two physical ports carry the
		# instruments' controls as well as the pad's.
		printf '{"port1":"dualshock2","port2":"dualshock2"}' > "$pd/settings"
		"$nat/run-native" "$pd" --frames 400 --screenshot "$work/pad2.idle.tga" >/dev/null 2>&1
		"$nat/run-native" "$pd" --frames 500 --press 400:100:8 --press 400:100:44 \
			--screenshot "$work/pad2.held.tga" >/dev/null 2>&1
		p1="$(python3 "$here/tests/pad-cells.py" "$work/pad2.idle.tga" "$work/pad2.held.tga" 0 | tr '\n' ' ' | sed 's/ $//')"
		p2="$(python3 "$here/tests/pad-cells.py" "$work/pad2.idle.tga" "$work/pad2.held.tga" 1 | tr '\n' ' ' | sed 's/ $//')"
		if [ "$p1" = "Circle" ] && [ "$p2" = "Cross" ]; then
			report "pad:ports" PASS "Circle on port 1 and Cross on port 2 reach their own pad and no other"
		else
			report "pad:ports" FAIL "port 1 shows [${p1:-nothing}] and port 2 shows [${p2:-nothing}], want Circle and Cross"
		fi
		printf '{}' > "$pd/settings"
	fi
fi

# ---- tier three: a disc ----------------------------------------------------
if [ -z "$disc" ]; then
	report "disc:boots" SKIP "no disc: put an image in tests/roms/"
	report "disc:equivalence" SKIP "would prove a game runs identically in the sandbox"
else
	dd_="$work/disc"
	mkdir -p "$dd_"
	cp "$bios" "$dd_/bios.bin"
	ln -s "$disc" "$dd_/$(basename "$disc")" 2>/dev/null || cp "$disc" "$dd_/"
	printf '{"disc":["%s"]}' "$(basename "$disc")" > "$dd_/slots"
	# fast boot: the bios animation is a minute of emulated time this gate does
	# not need to watch, and booting the disc's own program is the point.
	printf '{"fast_boot":true,"verbose":true}' > "$dd_/settings"

	# PCSX2 says what it loaded on the same stream as the digests, so the run
	# is kept whole and read twice.
	if ! "$nat/run-native" "$dd_" --frames 200 > "$work/disc.out" 2>"$work/disc.err"; then
		report "disc:boots" FAIL "native runner error: $(head -1 "$work/disc.err")"
	elif ! grep -q "ELF Loading: cdrom0" "$work/disc.out"; then
		report "disc:boots" FAIL "the disc's own program never loaded"
	else
		report "disc:boots" PASS "$(sed -n 's/.*ELF Loading: \(cdrom0[^,]*\), Game CRC = \([0-9A-F]*\).*/\1, CRC \2/p' "$work/disc.out" | head -1)"
	fi
	digests < "$work/disc.out" > "$work/disc.nat"

	if ! "$nat/run-wbx" "$gst/core.wbx" "$dd_" --frames 200 2>/dev/null | digests > "$work/disc.box"; then
		report "disc:equivalence" FAIL "waterbox runner error"
	elif cmp -s "$work/disc.nat" "$work/disc.box"; then
		report "disc:equivalence" PASS "200 frames of $(basename "$disc"), native == waterboxed"
	else
		report "disc:equivalence" FAIL "$(diff "$work/disc.nat" "$work/disc.box" | tr '\n' ' ' | head -c 120)"
	fi
fi

echo
echo "$ok ok, $failed failed, $skipped skipped"
[ "$failed" -eq 0 ]
