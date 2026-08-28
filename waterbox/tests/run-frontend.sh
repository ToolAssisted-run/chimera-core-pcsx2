#!/bin/bash
# The frontend half of the gate: load the PCSX2 package in Chimera (under Mono,
# on a private Xvfb display), boot a disc for a fixed number of frames with
# nothing pressed, and require the machine's memory to be byte-identical to the
# native reference. Then prove that a machine-shaping setting arrives through
# the frontend, and that the package's keybinds become the frontend's defaults.
#
# It needs the same content the core gate's second tier needs - a bios, and a
# disc - because a PlayStation 2 cannot run without them. Without that it
# reports SKIP and says so, rather than passing on nothing.
#
# Usage: ./run-frontend.sh [--chimera-root <path>] [--frames N]
set -u

here="$(cd "$(dirname "$0")" && pwd)"
wb="$(cd "$here/.." && pwd)"
root="$(cd "$wb/.." && pwd)"
frames=200
chimera_root=""
while [ $# -gt 0 ]; do
	case "$1" in
		--chimera-root) chimera_root="$2"; shift ;;
		--frames) frames="$2"; shift ;;
		-*) echo "unknown option: $1" >&2; exit 2 ;;
		*) break ;;
	esac
	shift
done

if [ -z "$chimera_root" ]; then
	for candidate in "$root/../chimera" "$HOME/chimera"; do
		[ -d "$candidate" ] && { chimera_root="$candidate"; break; }
	done
fi
[ -n "$chimera_root" ] && [ -d "$chimera_root" ] || {
	echo "chimera checkout not found; pass --chimera-root <path>" >&2; exit 1; }
chimera_root="$(cd "$chimera_root" && pwd)"

emu_exe="$chimera_root/build/Chimera.exe"
package="$chimera_root/build/Cores/pcsx2.chimeraCore"
rn="$root/build/meson-native/run-native"
[ -f "$emu_exe" ] || { echo "Chimera not built: $emu_exe" >&2; exit 1; }
[ -f "$package" ] || { echo "package not installed: $package (run ../build-package.sh)" >&2; exit 1; }
[ -x "$rn" ] || { echo "native reference not built" >&2; exit 1; }

ok=0
failed=0
skipped=0
report() {
	printf "%-28s %-9s %s\n" "$1" "$2" "$3"
	case "$2" in
		PASS) ok=$((ok + 1)) ;;
		SKIP) skipped=$((skipped + 1)) ;;
		*) failed=$((failed + 1)) ;;
	esac
}
printf "%-28s %-9s %s\n" "Check" "Result" "Detail"
printf "%-28s %-9s %s\n" "-----" "------" "------"

bios="${CHIMERA_PS2_BIOS:-}"
if [ -z "$bios" ]; then
	bios="$(find "$root/tests/roms/bios" -maxdepth 1 -name '*.bin' 2>/dev/null | sort | head -1)"
fi
disc="$(find "$root/tests/roms" -maxdepth 1 \( -name '*.iso' -o -name '*.bin' \) 2>/dev/null | sort | head -1)"

# Which bios this is, said out loud: the package declares one firmware entry per
# dump, each nailed to a "bios" setting value, so the project has to name the one
# it wants. The file the test found decides the value, which is what a user
# choosing that bios in the wizard would have stored.
bios_choice="$(basename "$bios" .bin)"

if [ -z "$bios" ] || [ -z "$disc" ]; then
	report "disc:frontend" SKIP "needs a bios in tests/roms/bios/ and a disc in tests/roms/"
	report "settings:clock" SKIP "would prove a machine-shaping setting reaches the guest"
	report "keybinds" SKIP "would prove the package's bindings become the frontend's"
	echo
	echo "$ok ok, $failed failed, $skipped skipped"
	exit 0
fi

work="$here/work"
mkdir -p "$work"

export LD_LIBRARY_PATH="$chimera_root/build/dll:$chimera_root/build:/usr/lib/x86_64-linux-gnu"
export MONO_CRASH_NOFILE=1 MONO_WINFORMS_XIM_STYLE=disabled ALSOFT_DRIVERS=null
xvfb_pid=""
cleanup() { [ -n "$xvfb_pid" ] && kill "$xvfb_pid" 2>/dev/null; }
trap cleanup EXIT
if [ -z "${DISPLAY:-}" ]; then
	command -v Xvfb >/dev/null || { echo "Xvfb not found (apt install xvfb)" >&2; exit 1; }
	for n in 90 91 92 93 94 95 96; do
		if [ ! -e "/tmp/.X11-unix/X$n" ]; then
			Xvfb ":$n" -screen 0 640x480x24 -nolisten tcp & xvfb_pid=$!
			export DISPLAY=":$n"; break
		fi
	done
	sleep 1
fi

config="$work/config.ini"
if [ ! -f "$config" ]; then
	( cd "$chimera_root" && timeout 120 mono "$emu_exe" --headless "--config=$config" \
		"--lua=$here/exit.lua" ) > "$work/bootstrap.log" 2>&1
	[ -f "$config" ] || { echo "config bootstrap failed (see $work/bootstrap.log)" >&2; exit 1; }
fi
sed -i 's/"DispMethod": [0-9]/"DispMethod": 1/' "$config"

SLICE=1048576

# The frontend resolves the bios through its firmware store, keyed by core name
# and declaration id - the same map the Firmware window writes.
firmware_json="$(python3 -c "import json,sys; print(json.dumps({'bios.bin': sys.argv[1]}))" "$bios")"

run_frontend() {
	local tag="$1" cfg="$2" nframes="$3" shot="${4:-}"
	local job="$work/job.$tag.txt"
	{
		echo "frames=$nframes"
		echo "out=$work/$tag.ram.bin"
		echo "meta=$work/$tag.meta.txt"
		echo "shot=$shot"
		echo "bytes=$SLICE"
	} > "$job"
	rm -f "$work/$tag.ram.bin" "$work/$tag.meta.txt"
	[ -n "$shot" ] && rm -f "$shot"
	( cd "$chimera_root" && MINIHAWK_JOB="$job" timeout 900 mono "$emu_exe" --headless \
		"--config=$cfg" "--core=$package" \
		"--lua=$here/frontend-ram.lua" "$disc" ) > "$work/$tag.log" 2>&1
	[ -f "$work/$tag.meta.txt" ] && grep -q "^status=OK" "$work/$tag.meta.txt"
}

# a native reference run: same disc, same idle schedule, settings JSON in $2
native_ram() {
	local tag="$1" settings="$2"
	local wd="$work/native.$tag"
	rm -rf "$wd"
	mkdir -p "$wd"
	cp "$bios" "$wd/bios.bin"
	ln -s "$disc" "$wd/$(basename "$disc")" 2>/dev/null || cp "$disc" "$wd/"
	printf '{"disc":["%s"]}' "$(basename "$disc")" > "$wd/slots"
	printf '%s' "${settings:-\{\}}" > "$wd/settings"
	"$rn" "$wd" --frames "$frames" --dump-domain "EE RAM" "$work/native.$tag.ram.full" \
		> "$work/native.$tag.txt" 2>&1 || return 1
	head -c "$SLICE" "$work/native.$tag.ram.full" > "$work/native.$tag.ram.bin"
}

settings_config() { python3 "$here/settings-config.py" "$config" "$1" "$2" "$3"; }

# --- the machine the frontend builds must be the one the gate signed off on ---
# Fast boot, so that the DISC is what runs. Without it two hundred frames are
# the console's own startup animation, which looks the same whether or not
# there is anything in the tray - and a leg that passes with an empty tray is
# a leg that proves the frontend mounted nothing. (It did, once: the core only
# knew how to find a disc through a project's slot map, and a rom opened
# directly arrives under the name waterbox.config calls "romFile".)
settings_config "$work/config.base.ini" "{\"fast_boot\": true, \"bios\": \"$bios_choice\"}" "$firmware_json"
if ! native_ram "base" '{"fast_boot":true}'; then
	report "disc:frontend" FAIL "native runner error (see tests/work/native.base.txt)"
elif ! run_frontend "base" "$work/config.base.ini" "$frames" "$work/base.png"; then
	report "disc:frontend" FAIL "no OK meta (see tests/work/base.log)"
elif ! cmp -s "$work/native.base.ram.bin" "$work/base.ram.bin"; then
	report "disc:frontend" FAIL "EE RAM differs from the native reference"
elif [ "$(sed -n 's/^ramsize=//p' "$work/base.meta.txt")" != "33554432" ]; then
	report "disc:frontend" FAIL "EE RAM is $(sed -n 's/^ramsize=//p' "$work/base.meta.txt") bytes, want 33554432"
else
	report "disc:frontend" PASS "$frames frames of $(basename "$disc"), EE RAM identical to the native reference"
fi

# --- a machine-shaping setting must reach the guest through the frontend -----
# The console's clock, which is the setting most worth proving: PCSX2 would
# otherwise read the computer's, and a machine that wakes at a different moment
# every run is a machine no movie replays on.
#
# The change shows up in the IOP's memory rather than the EE's, and that is not
# a detail to paper over: the date is held by the IOP module that drives the
# drive, and the EE only learns it when the game asks. Two hundred frames after
# a fast boot, the game has not asked yet - so the leg looks where the setting
# actually lands, and requires the EE side to match its native reference all
# the same.
settings_config "$work/config.rtc.ini" "{\"fast_boot\": true, \"rtc_year\": 7, \"bios\": \"$bios_choice\"}" "$firmware_json"
if ! native_ram "rtc" '{"fast_boot":true,"rtc_year":7}'; then
	report "settings:clock" FAIL "native runner error (see tests/work/native.rtc.txt)"
elif ! run_frontend "rtc" "$work/config.rtc.ini" "$frames"; then
	report "settings:clock" FAIL "run did not report OK (see tests/work/rtc.log)"
elif ! cmp -s "$work/native.rtc.ram.bin" "$work/rtc.ram.bin"; then
	report "settings:clock" FAIL "EE RAM differs from its native reference with rtc_year=7"
else
	base_iop="$(sed -n 's/^iophash=//p' "$work/base.meta.txt")"
	rtc_iop="$(sed -n 's/^iophash=//p' "$work/rtc.meta.txt")"
	if [ -z "$base_iop" ] || [ -z "$rtc_iop" ]; then
		report "settings:clock" FAIL "no IOP hash in the metadata"
	elif [ "$base_iop" = "$rtc_iop" ]; then
		report "settings:clock" FAIL "the clock setting never reached the machine"
	else
		report "settings:clock" PASS "rtc_year=7 matches its native reference and changes the IOP's memory"
	fi
fi

# --- the bindings the package ships must become the frontend's defaults ------
python3 "$here/forget-controller.py" "$work/config.base.ini" "$work/config.keys.ini" "DualShock 2"
if run_frontend "keys" "$work/config.keys.ini" 1; then
	if python3 "$here/check-keybinds.py" "$work/config.keys.ini" \
		"$wb/default_keybinds.json" "DualShock 2" > "$work/keys.txt" 2>&1; then
		report "keybinds" PASS "$(cat "$work/keys.txt")"
	else
		report "keybinds" FAIL "$(head -1 "$work/keys.txt")"
	fi
else
	report "keybinds" FAIL "run did not report OK (see tests/work/keys.log)"
fi

echo
echo "$ok ok, $failed failed, $skipped skipped"
[ "$failed" -eq 0 ]
