/* cinterface.cpp - a PlayStation 2 behind the chimera guest ABI.
 *
 * PCSX2 is an application, and the machine inside it is reached through
 * VMManager: a boot, a run loop, a shutdown, with a Host interface (host.cpp)
 * supplying everything a desktop would. What a frontend needs from a core is
 * narrower and stricter than what that run loop offers, and the difference is
 * this file:
 *
 *   ONE FRAME AT A TIME. VMManager::Execute() runs until something stops it,
 *   which on a desktop is the user. PCSX2 already has the primitive a movie
 *   needs - frame advance - so a Chimera frame is: arm frame advance for one
 *   frame, execute, and come back when the vsync boundary pauses the machine.
 *   The exit is PCSX2's own (Counters.cpp asks whether execution was
 *   interrupted at every vsync), not a hook this core added.
 *
 *   THE PICTURE, from memory. The software renderer draws into the emulated
 *   console's video memory and then hands the result to a graphics device;
 *   here that device is made of memory (waterbox/gs-device.cpp), so the
 *   finished frame is simply there to be read after every vsync.
 *
 *   THE SOUND, per frame. SPU2 mixes into a ring buffer that a device thread
 *   normally drains whenever it likes. Here the frame drains it
 *   (waterbox/audio-stream.cpp), so a frame's samples belong to that frame.
 *
 *   NO RECOMPILERS. The EE, the IOP and both vector units interpret, and the
 *   software rasteriser takes its C++ path. That is what makes this core
 *   possible inside a sandbox at all, and it is also what makes it slow;
 *   whether the recompilers can follow is the next question this port has to
 *   answer (docs/PLAN.md).
 *
 * This file compiles IDENTICALLY for the guest (miniBox emulibc) and for the
 * native reference build (native-shim/emulibc.h), which is what makes the
 * equivalence gate a real proof.
 */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <emulibc.h>
#include <waterbox_settings.h>
#include <waterbox_slots.h>

#include "CDVD/CDVD.h"
#include "CDVD/CDVDcommon.h"
#include "Config.h"
#include "GS/GS.h"
#include "Host.h"
#include "IopMem.h"
#include "Memory.h"
#include "SIO/Memcard/MemoryCardFile.h"
#include "SIO/Pad/Pad.h"
#include "SIO/Pad/PadDualshock2.h"
#include "SIO/Pad/PadGuitar.h"
#include "SIO/Pad/PadJogcon.h"
#include "SIO/Pad/PadNegcon.h"
#include "SIO/Pad/PadPopn.h"
#include "VMManager.h"
#include "VUmicro.h"
#include "common/Console.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "fmt/format.h"
#include "common/SettingsInterface.h"

/* ---------------------------------------------------------------------------
 * What the frontend sees. A PS2's picture has no fixed size - the display
 * circuits are programmed by the game - so the buffer here is the largest a
 * console can scan out, and the frame's real size travels with it.
 */
#define MAX_WIDTH 1280
#define MAX_HEIGHT 1024
#define MAX_SAMPLES 4096

static char g_loadError[512];
static uint32_t g_video[MAX_WIDTH * MAX_HEIGHT];
static int g_videoWidth = 640;
static int g_videoHeight = 448;
static int16_t g_soundOut[MAX_SAMPLES * 2];
static int g_nsamples;
static int g_inputRead;

/* Turbo. The patched GSRenderer::VSync reads this and skips the display stage -
 * the PCRTC merge and the deinterlace - while it is 0. The GS itself is
 * untouched: it consumes its FIFO and writes local memory exactly as it would
 * have, because that is memory the machine can read back.
 *
 * extern "C" and not static because the patched upstream file names it.
 * ECL_INVISIBLE because it is the frontend's policy for the moment, not part of
 * the machine: a state saved while fast-forwarding must not put the machine
 * back into it when it is loaded to be looked at. */
extern "C" { ECL_INVISIBLE int chimera_render_enabled = 1; }

/* The GS trace switch, read by gs-device.cpp and by the patched
 * GSRenderer::Merge. ECL_INVISIBLE for the same reason: a diagnostic is not
 * machine state, and a state saved with it off must not turn it off again. */
extern "C" { ECL_INVISIBLE int chimera_gs_trace = 0; }
static bool g_loaded;

/* The wire: EIGHT slots. Two physical ports, each of which a multitap turns
 * into four, in PCSX2's own unified-slot order (Sio.h): unified 0 and 1 are the
 * two ports, 2..4 are port 1's multitap slots B..D and 5..7 are port 2's. That
 * is the order a game sees, and P1..P8 are those eight.
 *
 * Order is the frontend's button order and must match waterbox.config: the
 * DualShock 2's seventeen for every slot, and for the two PHYSICAL ports the
 * twenty more the instruments need. A multitap slot has none of those, which is
 * why the block size differs and why every index goes through SlotButtons(). */
#define PS2_SLOTS 8
#define PS2_PORTS 2   /* the slots that may hold something other than a pad */
enum
{
	BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT,
	BTN_START, BTN_SELECT,
	BTN_SQUARE, BTN_CROSS, BTN_CIRCLE, BTN_TRIANGLE,
	BTN_L1, BTN_R1, BTN_L2, BTN_R2, BTN_L3, BTN_R3,
	BTN_ANALOG,
	BTN_DS2_COUNT,
	/* the two physical ports only, from here down */
	BTN_NEGCON_A = BTN_DS2_COUNT, BTN_NEGCON_B, BTN_NEGCON_I, BTN_NEGCON_II,
	BTN_STRUM_UP, BTN_STRUM_DOWN,
	BTN_FRET_GREEN, BTN_FRET_RED, BTN_FRET_YELLOW, BTN_FRET_BLUE, BTN_FRET_ORANGE,
	BTN_POP_WHITE_L, BTN_POP_YELLOW_L, BTN_POP_GREEN_L, BTN_POP_BLUE_L,
	BTN_POP_RED,
	BTN_POP_BLUE_R, BTN_POP_GREEN_R, BTN_POP_YELLOW_R, BTN_POP_WHITE_R,
	BTN_PORT_COUNT
};
#define BTN_COUNT (BTN_PORT_COUNT * PS2_PORTS + BTN_DS2_COUNT * (PS2_SLOTS - PS2_PORTS))
static uint8_t g_setButtons[BTN_COUNT];
static uint8_t g_buttons[BTN_COUNT];

/* the analog wire: two sticks for every slot, and for the two physical ports
 * the four the instruments read */
enum
{
	AXIS_LX, AXIS_LY, AXIS_RX, AXIS_RY,
	AXIS_DS2_COUNT,
	AXIS_DIAL = AXIS_DS2_COUNT, AXIS_TWIST, AXIS_WHAMMY, AXIS_TILT,
	AXIS_PORT_COUNT
};
#define AXIS_COUNT (AXIS_PORT_COUNT * PS2_PORTS + AXIS_DS2_COUNT * (PS2_SLOTS - PS2_PORTS))
static int16_t g_axes[AXIS_COUNT];

/* How many controls a slot declares, and where its block starts. The first two
 * slots are wider than the other six, so neither is a multiplication. */
static int SlotButtons(int slot) { return slot < PS2_PORTS ? BTN_PORT_COUNT : BTN_DS2_COUNT; }
static int SlotAxes(int slot) { return slot < PS2_PORTS ? AXIS_PORT_COUNT : AXIS_DS2_COUNT; }

static int SlotButtonBase(int slot)
{
	int base = 0;
	for (int i = 0; i < slot; i++) base += SlotButtons(i);
	return base;
}

static int SlotAxisBase(int slot)
{
	int base = 0;
	for (int i = 0; i < slot; i++) base += SlotAxes(i);
	return base;
}

/* What each slot is, read from the settings at Init. A slot's device is part of
 * the machine, not something that changes under a running movie. */
static Pad::ControllerType g_slotDevice[PS2_SLOTS];

/* the settings layer host.cpp installs, and the two things the sandbox's own
 * device and audio stream hand back */
SettingsInterface* ChimeraGetSettings();
/* The names the cards travel under. A frontend mounts what the last run left
 * under these names, and stores back what this run produced. */
static const char* const MEMCARD_NAME[PS2_SLOTS] = {
	"memcard1.ps2", "memcard2.ps2", "memcard3.ps2", "memcard4.ps2",
	"memcard5.ps2", "memcard6.ps2", "memcard7.ps2", "memcard8.ps2",
};

/* ...and what the CONSOLE remembers when it is switched off: the language, the
 * clock configuration, the region parameters, the machine's iLink id. PCSX2
 * names it after the bios file, and so does this. */
static const char* const NVRAM_NAME = "bios.nvm";

/* The GPU bridge (experiment; see waterbox/gl-bridge.h). Only where there is a
 * GL renderer to drive: the native reference build has the software rasteriser
 * and nothing else, so there is no GPU for it to be offered. */
#ifdef CHIMERA_GUEST_GL
typedef uint64_t (*chimera_gl_bridge_fn_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern "C" bool chimera_gl_bridge_start(chimera_gl_bridge_fn_t bridge);
#endif

extern "C" void ChimeraAdvanceClock(void);
extern "C" int ChimeraAudioPull(int16_t* out, int max_frames);
extern "C" bool ChimeraGSGetFrame(const u8** bits, int* pitch, int* width, int* height);

/* Lag detection: the machine looking at its input is what a lag frame IS. The
 * pad answers a poll from the SIO, which is where this is called from.
 */
extern "C" void chimera_input_was_read(void) { g_inputRead = 1; }

/* The buttons, as the pad container holds them. PCSX2's own Pad IS the
 * machine - the DualShock 2 protocol, the analog mode toggle, the pressure
 * sensitivity - so a frontend's input goes in there rather than into a
 * parallel copy of the same state.
 */
/* Is this a program the machine can be handed directly, rather than a disc?
 *
 * By its FIRST FOUR BYTES, not by its name. Upstream decides on the ".elf"
 * suffix, which is fine for a file picker and wrong here: a project names its
 * files whatever the author called them, and the slot map carries that name
 * into the guest verbatim. What makes a file a PS2 executable is that it is an
 * ELF, so that is what is asked.
 */
enum class ProgramKind
{
	NotAProgram,   /* a disc image, or anything else */
	Executable,    /* an EE program the machine can be started on */
	IopModule,     /* an .irx: a driver, loaded BY a program, never instead of one */
};

static ProgramKind ClassifyProgram(const char* path)
{
	auto fp = FileSystem::OpenManagedCFile(path, "rb");
	if (!fp)
		return ProgramKind::NotAProgram;
	unsigned char head[20] = {};
	if (std::fread(head, 1, sizeof(head), fp.get()) != sizeof(head))
		return ProgramKind::NotAProgram;
	if (head[0] != 0x7F || head[1] != 'E' || head[2] != 'L' || head[3] != 'F')
		return ProgramKind::NotAProgram;

	/* e_type. An IOP module is an ELF like any other except for this: Sony
	 * gave it 0xFF80, outside the range the standard assigns, which is what
	 * makes an .irx recognisable without reading its sections. */
	const unsigned type = head[16] | (static_cast<unsigned>(head[17]) << 8);
	return type == 0xFF80 ? ProgramKind::IopModule : ProgramKind::Executable;
}

/* One control, from the frontend's wire to a pad's input index. A pad takes a
 * FLOAT: the DualShock 2's buttons are pressure-sensitive, so "held" is 1.0 and
 * "not held" is 0.0, and a stick's half is however far it has been pushed. */
static void SetHalfAxis(PadBase* pad, u32 negative, u32 positive, int16_t value)
{
	const float v = static_cast<float>(value) / 32767.0f;
	pad->Set(negative, v < 0.0f ? -v : 0.0f);
	pad->Set(positive, v > 0.0f ? v : 0.0f);
}

static void ApplyInputSlot(int slot)
{
	PadBase* pad = Pad::GetPad(static_cast<u8>(slot));
	if (!pad)
		return;

	const uint8_t* btn = &g_buttons[SlotButtonBase(slot)];
	const int16_t* ax = &g_axes[SlotAxisBase(slot)];

	/* Every device has its OWN input indices - PadNegcon::PAD_A is not
	 * PadDualshock2::PAD_A - so the wire is translated per device rather than
	 * once. Only the device this slot actually holds is written; the controls
	 * of the others are declared for the frontend's sake and never read. */
	switch (g_slotDevice[slot])
	{
	case Pad::ControllerType::Guitar:
	{
		static const struct { int wire; u32 button; } map[] = {
			{BTN_STRUM_UP, PadGuitar::Inputs::STRUM_UP},
			{BTN_STRUM_DOWN, PadGuitar::Inputs::STRUM_DOWN},
			{BTN_SELECT, PadGuitar::Inputs::SELECT},
			{BTN_START, PadGuitar::Inputs::START},
			{BTN_FRET_GREEN, PadGuitar::Inputs::GREEN},
			{BTN_FRET_RED, PadGuitar::Inputs::RED},
			{BTN_FRET_YELLOW, PadGuitar::Inputs::YELLOW},
			{BTN_FRET_BLUE, PadGuitar::Inputs::BLUE},
			{BTN_FRET_ORANGE, PadGuitar::Inputs::ORANGE},
		};
		for (const auto& m : map)
			pad->Set(m.button, btn[m.wire] ? 1.0f : 0.0f);
		/* the whammy bar and the tilt sensor are single axes, not halves: what
		 * the frontend sends as -32768..32767 is 0..1 of a bar being pushed */
		pad->Set(PadGuitar::Inputs::WHAMMY,
			(static_cast<float>(ax[AXIS_WHAMMY]) + 32768.0f) / 65535.0f);
		pad->Set(PadGuitar::Inputs::TILT,
			(static_cast<float>(ax[AXIS_TILT]) + 32768.0f) / 65535.0f);
		return;
	}
	case Pad::ControllerType::Popn:
	{
		static const struct { int wire; u32 button; } map[] = {
			{BTN_POP_YELLOW_L, PadPopn::Inputs::PAD_YELLOW_LEFT},
			{BTN_POP_YELLOW_R, PadPopn::Inputs::PAD_YELLOW_RIGHT},
			{BTN_POP_BLUE_L, PadPopn::Inputs::PAD_BLUE_LEFT},
			{BTN_POP_BLUE_R, PadPopn::Inputs::PAD_BLUE_RIGHT},
			{BTN_POP_WHITE_L, PadPopn::Inputs::PAD_WHITE_LEFT},
			{BTN_POP_WHITE_R, PadPopn::Inputs::PAD_WHITE_RIGHT},
			{BTN_POP_GREEN_L, PadPopn::Inputs::PAD_GREEN_LEFT},
			{BTN_POP_GREEN_R, PadPopn::Inputs::PAD_GREEN_RIGHT},
			{BTN_POP_RED, PadPopn::Inputs::PAD_RED},
			{BTN_START, PadPopn::Inputs::PAD_START},
			{BTN_SELECT, PadPopn::Inputs::PAD_SELECT},
		};
		for (const auto& m : map)
			pad->Set(m.button, btn[m.wire] ? 1.0f : 0.0f);
		return;
	}
	case Pad::ControllerType::Negcon:
	{
		static const struct { int wire; u32 button; } map[] = {
			{BTN_UP, PadNegcon::Inputs::PAD_UP},
			{BTN_DOWN, PadNegcon::Inputs::PAD_DOWN},
			{BTN_LEFT, PadNegcon::Inputs::PAD_LEFT},
			{BTN_RIGHT, PadNegcon::Inputs::PAD_RIGHT},
			{BTN_NEGCON_A, PadNegcon::Inputs::PAD_A},
			{BTN_NEGCON_B, PadNegcon::Inputs::PAD_B},
			{BTN_NEGCON_I, PadNegcon::Inputs::PAD_I},
			{BTN_NEGCON_II, PadNegcon::Inputs::PAD_II},
			{BTN_START, PadNegcon::Inputs::PAD_START},
			/* the Negcon has ONE shoulder button each side, which is what L1
			 * and R1 are on a pad; there is no L2 or R2 to bind */
			{BTN_L1, PadNegcon::Inputs::PAD_L},
			{BTN_R1, PadNegcon::Inputs::PAD_R},
		};
		for (const auto& m : map)
			pad->Set(m.button, btn[m.wire] ? 1.0f : 0.0f);
		SetHalfAxis(pad, PadNegcon::Inputs::PAD_TWIST_LEFT,
			PadNegcon::Inputs::PAD_TWIST_RIGHT, ax[AXIS_TWIST]);
		return;
	}
	case Pad::ControllerType::Jogcon:
	{
		static const struct { int wire; u32 button; } map[] = {
			{BTN_UP, PadJogcon::Inputs::PAD_UP},
			{BTN_DOWN, PadJogcon::Inputs::PAD_DOWN},
			{BTN_LEFT, PadJogcon::Inputs::PAD_LEFT},
			{BTN_RIGHT, PadJogcon::Inputs::PAD_RIGHT},
			{BTN_TRIANGLE, PadJogcon::Inputs::PAD_TRIANGLE},
			{BTN_CIRCLE, PadJogcon::Inputs::PAD_CIRCLE},
			{BTN_CROSS, PadJogcon::Inputs::PAD_CROSS},
			{BTN_SQUARE, PadJogcon::Inputs::PAD_SQUARE},
			{BTN_SELECT, PadJogcon::Inputs::PAD_SELECT},
			{BTN_START, PadJogcon::Inputs::PAD_START},
			{BTN_L1, PadJogcon::Inputs::PAD_L1},
			{BTN_L2, PadJogcon::Inputs::PAD_L2},
			{BTN_R1, PadJogcon::Inputs::PAD_R1},
			{BTN_R2, PadJogcon::Inputs::PAD_R2},
		};
		for (const auto& m : map)
			pad->Set(m.button, btn[m.wire] ? 1.0f : 0.0f);
		SetHalfAxis(pad, PadJogcon::Inputs::PAD_DIAL_LEFT,
			PadJogcon::Inputs::PAD_DIAL_RIGHT, ax[AXIS_DIAL]);
		return;
	}
	case Pad::ControllerType::NotConnected:
		return;
	default:
		break; /* the DualShock 2, below */
	}

	static const struct
	{
		int wire;
		u32 button;
	} map[] = {
		{BTN_UP, PadDualshock2::Inputs::PAD_UP},
		{BTN_DOWN, PadDualshock2::Inputs::PAD_DOWN},
		{BTN_LEFT, PadDualshock2::Inputs::PAD_LEFT},
		{BTN_RIGHT, PadDualshock2::Inputs::PAD_RIGHT},
		{BTN_START, PadDualshock2::Inputs::PAD_START},
		{BTN_SELECT, PadDualshock2::Inputs::PAD_SELECT},
		{BTN_SQUARE, PadDualshock2::Inputs::PAD_SQUARE},
		{BTN_CROSS, PadDualshock2::Inputs::PAD_CROSS},
		{BTN_CIRCLE, PadDualshock2::Inputs::PAD_CIRCLE},
		{BTN_TRIANGLE, PadDualshock2::Inputs::PAD_TRIANGLE},
		{BTN_L1, PadDualshock2::Inputs::PAD_L1},
		{BTN_R1, PadDualshock2::Inputs::PAD_R1},
		{BTN_L2, PadDualshock2::Inputs::PAD_L2},
		{BTN_R2, PadDualshock2::Inputs::PAD_R2},
		{BTN_L3, PadDualshock2::Inputs::PAD_L3},
		{BTN_R3, PadDualshock2::Inputs::PAD_R3},
		{BTN_ANALOG, PadDualshock2::Inputs::PAD_ANALOG},
	};

	for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		pad->Set(map[i].button, btn[map[i].wire] ? 1.0f : 0.0f);

	/* The sticks: the frontend sends signed 16-bit, the pad wants a value per
	 * DIRECTION, which is how a DualShock 2's halves are reported. */
	static const struct
	{
		int axis;
		u32 negative;
		u32 positive;
	} sticks[] = {
		{AXIS_LX, PadDualshock2::Inputs::PAD_L_LEFT, PadDualshock2::Inputs::PAD_L_RIGHT},
		{AXIS_LY, PadDualshock2::Inputs::PAD_L_UP, PadDualshock2::Inputs::PAD_L_DOWN},
		{AXIS_RX, PadDualshock2::Inputs::PAD_R_LEFT, PadDualshock2::Inputs::PAD_R_RIGHT},
		{AXIS_RY, PadDualshock2::Inputs::PAD_R_UP, PadDualshock2::Inputs::PAD_R_DOWN},
	};

	for (size_t i = 0; i < sizeof(sticks) / sizeof(sticks[0]); i++)
		SetHalfAxis(pad, sticks[i].negative, sticks[i].positive, ax[sticks[i].axis]);
}

/* Every slot, every frame. A slot set to 'none' holds a PadNotConnected, which
 * has nothing to write to. */
static void ApplyInput()
{
	for (int slot = 0; slot < PS2_SLOTS; slot++)
		ApplyInputSlot(slot);
}

/* ---------------------------------------------------------------------------
 * What kind of machine this is, from the project.
 *
 * Everything here is a decision a movie has to carry: a machine that boots a
 * different bios, or interprets where another recompiled, is a different
 * machine. None of it is a display preference.
 */
static void ApplySettings(SettingsInterface& si, bool verbose)
{
	/* The bios. There is no HLE alternative on a PS2 - nothing boots without
	 * one - so the frontend guarantees the file is mounted (the firmware
	 * declaration in waterbox.config).
	 *
	 * It always arrives under the DECLARATION ID, whichever dump the project
	 * chose: the package declares one firmware entry per known bios, each
	 * nailed to a value of the "bios" setting, and the frontend resolves the
	 * one the project asked for and mounts it here. So this core opens one
	 * name, and which console it is was decided before it ran. */
	si.SetStringValue("Filenames", "BIOS", "bios.bin");

	/* PCSX2 says a great deal about what it is doing, and a core that swallowed
	 * it would be a core nobody can debug. It goes to stderr, where a gate and a
	 * bug report can both find it, and only when the project asks - through
	 * PCSX2's own logging settings, because it reapplies them on every settings
	 * load and would otherwise turn the log back off a moment later. */
	si.SetBoolValue("Logging", "EnableSystemConsole", verbose);
	si.SetBoolValue("Logging", "EnableVerbose", verbose);
	si.SetBoolValue("Logging", "EnableFileLogging", false);

	/* ...and the folders it is all mounted in, which is one folder. */
	static const char* const folders[] = {"Bios", "Snapshots", "Savestates", "MemoryCards", "Logs",
		"Cheats", "Patches", "Covers", "GameSettings", "UserResources", "Cache", "Textures",
		"InputProfiles", "Videos", "DebuggerLayouts", "DebuggerSettings"};
	for (size_t i = 0; i < sizeof(folders) / sizeof(folders[0]); i++)
		si.SetStringValue("Folders", folders[i], ".");

	/* Interpreters, all of them, and no fastmem.
	 *
	 * Fastmem reserves a 4GB window so a recompiler can address PS2 memory
	 * without a lookup. A sandbox has no such window, and with nothing
	 * recompiling there is nothing to speed up: the interpreters go through
	 * the vtlb's page table either way. */
	si.SetBoolValue("EmuCore/CPU/Recompiler", "EnableEE", false);
	si.SetBoolValue("EmuCore/CPU/Recompiler", "EnableIOP", false);
	si.SetBoolValue("EmuCore/CPU/Recompiler", "EnableVU0", false);
	si.SetBoolValue("EmuCore/CPU/Recompiler", "EnableVU1", false);
	si.SetBoolValue("EmuCore/CPU/Recompiler", "EnableEECache", false);
	si.SetBoolValue("EmuCore/CPU/Recompiler", "EnableFastmem", false);

	/* Which renderer draws, with no threads of its own either way.
	 *
	 * Without a guest Mesa, patch 0002 leaves nothing in the build but the
	 * software renderer. With one, PCSX2's OpenGL renderer is here too, and it
	 * draws through a Mesa softpipe compiled into this core: still no GPU,
	 * still nothing asked of the machine this runs on, so both are
	 * deterministic and a movie made under one replays under the other.
	 * See waterbox/gl-osmesa.cpp. */
	GSRendererType gsRenderer = GSRendererType::SW;
#ifdef CHIMERA_GUEST_GL
	{
		char value[16] = "software";
		wbx_setting_str("renderer", value, sizeof(value));
		/* "opengl-hw" asks for a real GPU, and gets one only if the host
		 * offered a bridge before Init. When it did not - no context, an old
		 * driver, a frontend that does not do this - the softpipe draws and the
		 * machine is the deterministic one. Silently, on purpose: a project
		 * that cannot have a GPU today should still run. */
		if (!strcmp(value, "opengl") || !strcmp(value, "opengl-hw"))
			gsRenderer = GSRendererType::OGL;
	}
#endif
	si.SetIntValue("EmuCore/GS", "Renderer", static_cast<int>(gsRenderer));
	si.SetIntValue("EmuCore/GS", "extrathreads", 0);

	/* A setting whose value is one of a list, as an index into that list. The
	 * package declares the names (see waterbox.config); PCSX2 wants numbers. */
	auto SettingIndex = [](const char* name, const char* const* options, int count, int fallback) {
		char value[32];
		std::strncpy(value, options[fallback], sizeof(value) - 1);
		value[sizeof(value) - 1] = '\0';
		wbx_setting_str(name, value, sizeof(value));
		for (int i = 0; i < count; i++)
			if (!std::strcmp(value, options[i]))
				return i;
		return fallback;
	};

	/* How the two fields become one picture.
	 *
	 * A PS2 in an interlaced mode scans out ONE FIELD per vsync: 224 lines of
	 * a 448 line image, the other 224 arriving the next time round. A core
	 * hands the frontend one picture per frame, so something has to decide
	 * what a single field looks like as a whole frame - and the answer is
	 * visible in every still screen. Handing the field over stretched, which
	 * is what "off" does and what this core used to do always, makes a static
	 * picture climb two scanlines and fall back every frame: the fields are
	 * two different pictures and nothing puts them in their proper places.
	 * Measured on the PS2 browser's own menu, a still screen: 0.00 average
	 * change per frame woven, 3.00 with no deinterlacing at all.
	 *
	 * "auto" is the default and asks the MACHINE. A PS2 does not always draw
	 * fields: plenty of games render whole frames, and putting a whole frame
	 * through a deinterlacer destroys half of it - the picture then carries one
	 * row of this frame, one row of the last, forever. That is what this core
	 * did to Street Fighter EX3 and TimeSplitters until 2026-08-29, and it is
	 * why the default is no longer a fixed choice.
	 *
	 * PCSX2's own "Automatic" reads SMODE2.FFMD, the scanmask, and whether the
	 * game is deinterlacing itself; it does NOTHING when the answer is whole
	 * frames and reaches for the MOTION ADAPTIVE deinterlacer when it is
	 * fields. This core follows that decision exactly, MAD included.
	 *
	 * MAD is here, where a first reading of the problem said it could not be.
	 * It compares this field with the last three and decides per pixel whether
	 * to weave a line or invent one, and the three fields it remembers live in
	 * a texture - which on a GPU would be outside the savestate and would make
	 * a movie's picture depend on how you reached a frame. In THIS core the
	 * device's textures are the guest's own memory (waterbox/gs-device.cpp), so
	 * that history is in the state like everything else, and the gate's
	 * per-frame save/load round-trip is what proves it.
	 *
	 * See waterbox/gs-device.cpp for the software renderer's copies of the
	 * same shaders: a project must draw the same picture whichever renderer it
	 * chose. */
	{
		static const char* const kNames[] = { "auto", "adaptive", "adaptive-bff",
			"weave", "weave-bff", "blend", "bob", "off" };
		static const GSInterlaceMode kModes[] = {
			GSInterlaceMode::Automatic,
			GSInterlaceMode::AdaptiveTFF, GSInterlaceMode::AdaptiveBFF,
			GSInterlaceMode::WeaveTFF, GSInterlaceMode::WeaveBFF,
			GSInterlaceMode::BlendTFF, GSInterlaceMode::BobTFF, GSInterlaceMode::Off };
		const int choice = SettingIndex("deinterlace", kNames, 8, 0);
		si.SetIntValue("EmuCore/GS", "deinterlace_mode", static_cast<int>(kModes[choice]));
	}
	si.SetBoolValue("EmuCore/GS", "VsyncEnable", false);
	si.SetBoolValue("EmuCore/GS", "OsdShowMessages", false);
	si.SetBoolValue("EmuCore/GS", "OsdShowSpeed", false);
	si.SetBoolValue("EmuCore/GS", "OsdShowFPS", false);

	/* No time stretching.
	 *
	 * SPU2's default sync mode RESHAPES the samples - it stretches or squeezes
	 * them to keep an audio device's buffer from running dry, which is exactly
	 * the right thing for an emulator with a sound card and exactly the wrong
	 * thing for a core. A frame's samples are the ones the SPU2 produced; what
	 * a frontend does about buffering afterwards is its own business. */
	si.SetStringValue("SPU2/Output", "SyncMode", "Disabled");

	/* No MTVU: a vector unit on a second thread, and there is one thread. */
	si.SetBoolValue("EmuCore/Speedhacks", "vuThread", false);

	/* PCSX2's own recording tools stay off; Chimera records the movie. Its
	 * patch database stays off because a machine that rewrites the game's code
	 * behind a movie's back is not one the movie replays on. */
	si.SetBoolValue("EmuCore", "EnableRecordingTools", false);
	si.SetBoolValue("EmuCore", "EnableDiscordPresence", false);
	si.SetBoolValue("EmuCore", "EnableCheats", false);
	si.SetBoolValue("EmuCore", "EnableWideScreenPatches", false);
	si.SetBoolValue("EmuCore", "EnableNoInterlacingPatches", false);
	si.SetBoolValue("EmuCore", "EnablePatches", false);
	si.SetBoolValue("EmuCore", "SaveStateOnShutdown", false);

	/* The memory cards.
	 *
	 * Which slots have a card in them is part of what the machine IS - a game
	 * behaves differently with and without one, and asks about it on the first
	 * screen - so the project decides, and the default is what a console
	 * usually looks like: one card, in slot one.
	 *
	 * The cards themselves never touch a file (patch 0009): each is a buffer,
	 * filled from whatever the frontend mounted under its name and handed back
	 * through the save-data channel afterwards.
	 */
	/* Eight of them, indexed the way PCSX2 indexes everything on the SIO bus:
	 * unified slot 0 and 1 are the console's own two, and 2..7 are the six a
	 * multitap adds. The console's two are "Slot<n>", the rest are
	 * "Multitap<port>_Slot<slot>", which is the same numbering wearing a
	 * different name (Pcsx2Config.cpp's LoadSaveMemcards). */
	for (int card = 0; card < PS2_SLOTS; card++)
	{
		const bool present = wbx_setting_bool(
			fmt::format("memcard{}", card + 1).c_str(), card == 0 ? 1 : 0) != 0;
		if (card < 2)
		{
			si.SetBoolValue("MemoryCards", fmt::format("Slot{}_Enable", card + 1).c_str(), present);
			si.SetStringValue("MemoryCards", fmt::format("Slot{}_Filename", card + 1).c_str(),
				MEMCARD_NAME[card]);
		}
		else
		{
			const int port = card <= 4 ? 1 : 2;
			const int slot = (card <= 4 ? card - 1 : card - 4) + 1;
			si.SetBoolValue("MemoryCards",
				fmt::format("Multitap{}_Slot{}_Enable", port, slot).c_str(), present);
			si.SetStringValue("MemoryCards",
				fmt::format("Multitap{}_Slot{}_Filename", port, slot).c_str(), MEMCARD_NAME[card]);
		}
	}

	/* The multitaps, and what is plugged into each of the eight slots.
	 *
	 * A multitap is its own setting rather than something guessed from the
	 * controllers: a game can SEE one whether or not anything is in it, and it
	 * is what makes the six extra memory card slots reachable at all - so
	 * deriving it would make a card in a slot depend on a pad being in it.
	 *
	 * The pad type lives in section "Pad<unified+1>", key "Type", under the
	 * names PCSX2 gives its own ControllerInfo. "None" is a real device here
	 * (PadNotConnected), not an absence. */
	si.SetBoolValue("Pad", "MultitapPort1", wbx_setting_bool("multitap1", 0) != 0);
	si.SetBoolValue("Pad", "MultitapPort2", wbx_setting_bool("multitap2", 0) != 0);

	{
		static const char* const devices[] = {
			"none", "dualshock2", "guitar", "jogcon", "negcon", "popn"
		};
		static const char* const pcsx2Names[] = {
			"None", "DualShock2", "Guitar", "Jogcon", "Negcon", "Popn"
		};
		static const Pad::ControllerType types[] = {
			Pad::ControllerType::NotConnected, Pad::ControllerType::DualShock2,
			Pad::ControllerType::Guitar, Pad::ControllerType::Jogcon,
			Pad::ControllerType::Negcon, Pad::ControllerType::Popn,
		};
		for (int slot = 0; slot < PS2_SLOTS; slot++)
		{
			/* the instruments are offered on the two physical ports only, so
			 * the other six choose between two options and nothing else */
			const int choices = slot < PS2_PORTS ? 6 : 2;
			const int choice = SettingIndex(
				fmt::format("port{}", slot + 1).c_str(), devices, choices, slot == 0 ? 1 : 0);
			g_slotDevice[slot] = types[choice];
			si.SetStringValue(fmt::format("Pad{}", slot + 1).c_str(), "Type", pcsx2Names[choice]);
		}
	}

	/* Auto-eject exists so that a card swapped on a desktop is noticed. A
	 * project's cards do not change while the machine runs, and a card that
	 * ejected itself at an unpredictable moment is not something a movie can
	 * replay. */
	si.SetIntValue("EmuCore", "McdEjectTimeout", 0);
	si.SetBoolValue("EmuCore", "McdEnableEjection", false);
	si.SetBoolValue("EmuCore", "McdFolderAutoManage", false);

	/* The machine's clock.
	 *
	 * A PS2 has a battery-backed clock, and PCSX2 reads the computer's: the
	 * date goes into the machine at boot, where the bios and games read it,
	 * and some games seed their random numbers from it. Two runs of the same
	 * movie would then start from two different machines - the same bug this
	 * project already found in Flycast, and PCSX2 already has the switch for
	 * it, because its own recorder needed the same thing.
	 *
	 * So the clock comes from the project. The default is the date PCSX2's
	 * recorder uses (2020-03-04), which is late enough that every PS2 game
	 * accepts it as a plausible today. */
	si.SetBoolValue("EmuCore", "ManuallySetRealTimeClock", true);
	si.SetIntValue("EmuCore", "RtcYear", static_cast<int>(wbx_setting_long("rtc_year", 20)));
	si.SetIntValue("EmuCore", "RtcMonth", static_cast<int>(wbx_setting_long("rtc_month", 3)));
	si.SetIntValue("EmuCore", "RtcDay", static_cast<int>(wbx_setting_long("rtc_day", 4)));
	si.SetIntValue("EmuCore", "RtcHour", static_cast<int>(wbx_setting_long("rtc_hour", 0)));
	si.SetIntValue("EmuCore", "RtcMinute", static_cast<int>(wbx_setting_long("rtc_minute", 0)));
	si.SetIntValue("EmuCore", "RtcSecond", static_cast<int>(wbx_setting_long("rtc_second", 0)));

	/* Fast boot skips the bios splash. It is a machine difference, so the
	 * project says which - and it says yes by default: twenty seconds of console
	 * animation sits at the front of every run made without it, and a project
	 * that actually wants the splash can ask for it. */
	si.SetBoolValue("EmuCore", "EnableFastBoot", wbx_setting_bool("fast_boot", 1) != 0);
}

/* ---------------------------------------------------------------------------
 * the chimera guest ABI is a C ABI: the adapter looks these up by name
 */
extern "C" {

ECL_EXPORT const char* GetLoadError(void) { return g_loadError; }

ECL_EXPORT int Init(void)
{
	g_loadError[0] = '\0';

	/* PCSX2 says a great deal about what it is doing, and a core that swallowed
	 * it would be a core nobody can debug. It goes to stderr, where a gate and a
	 * bug report can both find it, and only when the project asks. */
	const bool verbose = wbx_setting_bool("verbose", 0) != 0;
	/* verbose covers the picture too: how each frame is assembled, and which
	 * deinterlacer the machine's own display mode led to (gs-device.cpp). */
	chimera_gs_trace = verbose ? 1 : 0;

	/* Everything the machine is made of is mounted at the root of the guest's
	 * file system: the bios through the firmware channel, the disc through a
	 * file slot. There is nowhere else to look, and nowhere to write.
	 *
	 * These are stated twice on purpose. EmuFolders::LoadConfig rebuilds every
	 * one of them from the settings a moment later, so the assignments below
	 * are what the machine starts with and the "Folders" entries are what it
	 * keeps.
	 */
	EmuFolders::AppRoot = "./";
	EmuFolders::DataRoot = "./";
	EmuFolders::Resources = "./";
	EmuFolders::UserResources = "./";
	EmuFolders::Bios = "./";
	EmuFolders::Settings = "./";
	EmuFolders::MemoryCards = "./";
	EmuFolders::Savestates = "./";
	EmuFolders::Snapshots = "./";
	EmuFolders::Logs = "./";
	EmuFolders::Cheats = "./";
	EmuFolders::Patches = "./";
	EmuFolders::Cache = "./";
	EmuFolders::Covers = "./";
	EmuFolders::GameSettings = "./";
	EmuFolders::Textures = "./";
	EmuFolders::InputProfiles = "./";
	EmuFolders::Videos = "./";

	SettingsInterface* si = ChimeraGetSettings();
	VMManager::SetDefaultSettings(*si, true, true, true, true, true);
	ApplySettings(*si, verbose);
	VMManager::Internal::LoadStartupSettings();

	/* The machine's memory is allocated inside CPUThreadInitialize, before any
	 * settings have reached EmuConfig - so the two decisions that change what
	 * gets allocated have to be made here, by hand, or the sandbox is asked for
	 * four gigabytes of fastmem it does not have. */
	EmuConfig.Cpu.Recompiler.EnableFastmem = false;
	EmuConfig.Cpu.Recompiler.EnableEE = false;
	EmuConfig.Cpu.Recompiler.EnableIOP = false;
	EmuConfig.Cpu.Recompiler.EnableVU0 = false;
	EmuConfig.Cpu.Recompiler.EnableVU1 = false;

	if (!VMManager::Internal::CPUThreadInitialize())
	{
		snprintf(g_loadError, sizeof(g_loadError), "the machine would not initialise");
		return 0;
	}

	VMManager::ApplySettings();

	/* The disc, if the project has one. A PS2 with no disc is a PS2 sitting at
	 * its own bios menu, which is a machine worth being able to boot: it is
	 * what proves the core runs before any content exists. */
	/* Two ways a disc arrives, and both must work. A PROJECT names its files,
	 * and the slot map says which is the disc. A rom opened directly - the
	 * frontend's command line, and the way a package is usually tried first -
	 * is mounted under the name waterbox.config gives as "romFile", with no
	 * slot map at all. Handling only the first is a core that works in a
	 * project and boots an empty tray everywhere else, which is exactly what
	 * the first frontend run of this core did. */
	/* The save data a project starts from, if it brought any.
	 *
	 * The files are mounted under their own names and the machine finds them
	 * that way - the memory cards and the NVRAM each open the name they always
	 * open, whether the frontend supplied one or not. Nothing here has to load
	 * them; what this does is REFUSE a file the machine would silently ignore,
	 * because a project carrying someone's saved game under a name nothing
	 * reads, booting to a blank card, is worse than one that will not boot. */
	{
		static const char* const kKnownSaves[] = { "memcard1.ps2", "memcard2.ps2", "bios.nvm" };
		char entry[512];
		const int32_t saves = wbx_slot_count("savedata");
		for (int32_t i = 0; i < saves; i++)
		{
			if (wbx_slot_name("savedata", i, entry, sizeof(entry)) == nullptr)
				continue;
			bool known = false;
			for (const char* known_name : kKnownSaves)
				if (!strcmp(entry, known_name)) { known = true; break; }
			if (!known)
			{
				snprintf(g_loadError, sizeof(g_loadError),
					"this machine does not read save data called \"%s\". It reads "
					"memcard1.ps2, memcard2.ps2 and bios.nvm - the names Export Save "
					"Data writes.", entry);
				return 0;
			}
		}
	}

	VMBootParameters boot;
	char name[512];
	const char* file = "disc";
	if (wbx_slot_count("disc") > 0 && wbx_slot_name("disc", 0, name, sizeof(name)) != nullptr)
		file = name;

	if (FileSystem::FileExists(file))
	{
		const ProgramKind kind = ClassifyProgram(file);
		if (kind == ProgramKind::IopModule)
		{
			snprintf(g_loadError, sizeof(g_loadError),
				"\"%s\" is an IOP module (.irx). Those are drivers a program loads "
				"while it runs - a memory card driver, a pad driver - and not "
				"something the machine can be started on. Give it the program "
				"instead: a disc image, or the .elf that would have loaded this.",
				file);
			return 0;
		}
		if (kind == ProgramKind::Executable)
		{
			/* A PS2 executable, run directly - which is what a PS2 does when
			 * a disc's own boot file is handed to it, and what every homebrew
			 * and test program in the world ships as. There is no disc in the
			 * tray: the machine boots its bios and is given this to run
			 * instead, which is upstream's elf_override and also what forces
			 * the fast boot it needs. */
			boot.elf_override = file;
			boot.source_type = CDVD_SourceType::NoDisc;
		}
		else
		{
			boot.filename = file;
			boot.source_type = CDVD_SourceType::Iso;
		}
	}
	else
	{
		/* No disc is a machine too: a console sitting at its own browser. */
		boot.source_type = CDVD_SourceType::NoDisc;
	}

	Error error;
	if (VMManager::Initialize(boot, &error) != VMBootResult::StartupSuccess)
	{
		snprintf(g_loadError, sizeof(g_loadError), "%s", error.GetDescription().c_str());
		return 0;
	}

	VMManager::SetState(VMState::Running);
	g_loaded = true;
	return 1;
}

ECL_EXPORT void SetButton(int index, int value)
{
	if (index >= 0 && index < BTN_COUNT)
		g_setButtons[index] = value ? 1 : 0;
}

/* WHICH DECLARED CONTROLS THIS MACHINE HAS.
 *
 * waterbox.config declares the union of every device the eight slots can hold,
 * because a declaration is static and cannot know what a project plugged in.
 * The slot settings are read here, so here is where the question is answered.
 *
 * Each list below is the device's OWN Inputs enum (SIO/Pad/Pad*.h), which is
 * the same place ApplyInputSlot translates the wire into - so a device that
 * grows a button grows a column, and one that never had a stick never shows
 * one. The wire is untouched: a slot's block is the size it always was and
 * every index in ApplyInputSlot stays where it is. */
static bool WireLiveFor(Pad::ControllerType device, int wire)
{
	switch (device)
	{
		case Pad::ControllerType::DualShock2:
			return wire < BTN_DS2_COUNT;

		case Pad::ControllerType::Guitar:
			/* a strum bar, five frets, and the two the shell still has */
			return wire == BTN_START || wire == BTN_SELECT
				|| wire == BTN_STRUM_UP || wire == BTN_STRUM_DOWN
				|| (wire >= BTN_FRET_GREEN && wire <= BTN_FRET_ORANGE);

		case Pad::ControllerType::Jogcon:
			/* a DualShock 2 without its sticks: no L3, no R3, no analog
			 * button, and a dial where the sticks were */
			return wire <= BTN_R2 && wire != BTN_L3 && wire != BTN_R3
				&& wire != BTN_ANALOG;

		case Pad::ControllerType::Negcon:
			/* a d-pad, its own four buttons, Start, and ONE shoulder each side
			 * - which is what L1 and R1 are here (PadNegcon::PAD_L/PAD_R) */
			return wire <= BTN_RIGHT || wire == BTN_START
				|| wire == BTN_L1 || wire == BTN_R1
				|| (wire >= BTN_NEGCON_A && wire <= BTN_NEGCON_II);

		case Pad::ControllerType::Popn:
			return wire == BTN_START || wire == BTN_SELECT
				|| (wire >= BTN_POP_WHITE_L && wire <= BTN_POP_WHITE_R);

		default: /* NotConnected */
			return false;
	}
}

ECL_EXPORT int IsButtonActive(int index)
{
	if (index < 0 || index >= BTN_COUNT) return 0;
	for (int slot = 0; slot < PS2_SLOTS; slot++)
	{
		const int base = SlotButtonBase(slot);
		if (index < base + SlotButtons(slot))
			return WireLiveFor(g_slotDevice[slot], index - base) ? 1 : 0;
	}
	return 0;
}

ECL_EXPORT int IsAxisActive(int index)
{
	if (index < 0 || index >= AXIS_COUNT) return 0;
	for (int slot = 0; slot < PS2_SLOTS; slot++)
	{
		const int base = SlotAxisBase(slot);
		if (index >= base + SlotAxes(slot)) continue;
		const int wire = index - base;
		switch (g_slotDevice[slot])
		{
			case Pad::ControllerType::DualShock2:
				return wire < AXIS_DS2_COUNT ? 1 : 0;
			case Pad::ControllerType::Jogcon:
				return wire == AXIS_DIAL ? 1 : 0;
			case Pad::ControllerType::Negcon:
				return wire == AXIS_TWIST ? 1 : 0;
			case Pad::ControllerType::Guitar:
				return wire == AXIS_WHAMMY || wire == AXIS_TILT ? 1 : 0;
			default: /* a Pop'n controller is nine buttons and nothing else */
				return 0;
		}
	}
	return 0;
}

ECL_EXPORT void SetAxis(int index, int value)
{
	if (index >= 0 && index < AXIS_COUNT)
		g_axes[index] = static_cast<int16_t>(value);
}

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
	/* Two input channels, and a frame is the UNION of them: the first 64
	 * buttons arrive packed in this call, while the gate harness (and any
	 * controller wider than 64) drives SetButton.
	 *
	 * EIGHT SLOTS IS WIDER THAN 64. Shifting a uint64_t by 64 or more is
	 * undefined, not zero, so the packed channel stops where it runs out and
	 * the rest of the wire is SetButton's alone - which is what Chimera uses
	 * for a controller this wide anyway. */
	for (int i = 0; i < BTN_COUNT; i++)
		g_buttons[i] = g_setButtons[i] || (i < 64 && ((packed >> i) & 1));

	g_inputRead = 0;
	g_nsamples = 0;

	if (!g_loaded)
		return;

	ApplyInput();
	ChimeraAdvanceClock();

	/* One frame: arm PCSX2's own frame advance, run, and return when the next
	 * vsync boundary pauses the machine. */
	VMManager::FrameAdvance(1);
	VMManager::Execute();

	g_nsamples = ChimeraAudioPull(g_soundOut, MAX_SAMPLES);
}

/* The picture, out of the device made of memory. A frame the machine never
 * presented leaves the last one standing, which is what the hardware does too:
 * the video circuits keep scanning out whatever is in the frame buffer.
 */
/* Turbo (optional guest ABI group): while off the core must produce no picture
 * and must otherwise be exactly the machine it would have been. run-gate.sh's
 * turbo leg is the proof - half a run undrawn leaves the same machine, and the
 * same pictures once drawing resumes. */
ECL_EXPORT void SetRenderingEnabled(int on) { chimera_render_enabled = on != 0; }

ECL_EXPORT uint32_t* GetVideoBgra(void)
{
	const u8* bits = nullptr;
	int pitch = 0, width = 0, height = 0;
	if (!ChimeraGSGetFrame(&bits, &pitch, &width, &height))
		return g_video;

	width = (width > MAX_WIDTH) ? MAX_WIDTH : width;
	height = (height > MAX_HEIGHT) ? MAX_HEIGHT : height;
	g_videoWidth = width;
	g_videoHeight = height;

	/* The GS device holds RGBA8; a frontend takes BGRA. */
	for (int y = 0; y < height; y++)
	{
		const uint32_t* src = reinterpret_cast<const uint32_t*>(bits + static_cast<size_t>(y) * pitch);
		uint32_t* dst = &g_video[static_cast<size_t>(y) * width];
		for (int x = 0; x < width; x++)
		{
			const uint32_t p = src[x];
			dst[x] = (p & 0xFF00FF00u) | ((p & 0x00FF0000u) >> 16) | ((p & 0x000000FFu) << 16);
		}
	}

	return g_video;
}

ECL_EXPORT int GetVideoWidth(void) { return g_videoWidth; }
ECL_EXPORT int GetVideoHeight(void) { return g_videoHeight; }

ECL_EXPORT int16_t* GetAudio(void) { return g_soundOut; }
ECL_EXPORT int GetAudioSampleCount(void) { return g_nsamples; }

/* An NTSC PS2 scans out at 59.94Hz, as the exact ratio rather than a float. */
ECL_EXPORT int GetVsyncNumerator(void) { return 60000; }
ECL_EXPORT int GetVsyncDenominator(void) { return 1001; }

ECL_EXPORT int InputWasRead(void) { return g_inputRead; }

/* ---------------------------------------------------------------------------
 * Memory domains: the machine's memory as a watch window and a movie see it.
 *
 * A PS2 has two computers in it. The EE is the main one with 32MB; the IOP is
 * a PS1 with 2MB that handles the disc, the controllers and the sound. Both
 * matter to a tool-assisted run, and so does the scratchpad, which is where
 * the EE keeps what it is working on right now.
 */
struct Domain
{
	const char* name;
	uint8_t* (*ptr)();
	int64_t (*size)();
};

static uint8_t* EeRamPtr() { return eeMem ? eeMem->Main : nullptr; }
static int64_t EeRamSize() { return Ps2MemSize::MainRam; }
static uint8_t* IopRamPtr() { return iopMem ? iopMem->Main : nullptr; }
static int64_t IopRamSize() { return Ps2MemSize::IopRam; }
static uint8_t* ScratchPtr() { return eeMem ? eeMem->Scratch : nullptr; }
static int64_t ScratchSize() { return Ps2MemSize::Scratch; }
static uint8_t* Vu0Ptr() { return reinterpret_cast<uint8_t*>(vuRegs[0].Mem); }
static int64_t Vu0Size() { return 0x1000; }
static uint8_t* Vu1Ptr() { return reinterpret_cast<uint8_t*>(vuRegs[1].Mem); }
static int64_t Vu1Size() { return 0x4000; }

static const Domain g_domains[] = {
	{"EE RAM", EeRamPtr, EeRamSize},
	{"IOP RAM", IopRamPtr, IopRamSize},
	{"EE Scratchpad", ScratchPtr, ScratchSize},
	{"VU0 RAM", Vu0Ptr, Vu0Size},
	{"VU1 RAM", Vu1Ptr, Vu1Size},
};
#define DOMAIN_COUNT ((int)(sizeof(g_domains) / sizeof(g_domains[0])))

ECL_EXPORT int GetMemoryDomainCount(void) { return DOMAIN_COUNT; }

ECL_EXPORT const char* GetMemoryDomainName(int which)
{
	return (which >= 0 && which < DOMAIN_COUNT) ? g_domains[which].name : nullptr;
}

ECL_EXPORT uint8_t* GetMemoryDomainPtr(int which)
{
	return (which >= 0 && which < DOMAIN_COUNT) ? g_domains[which].ptr() : nullptr;
}

ECL_EXPORT int64_t GetMemoryDomainSize(int which)
{
	return (which >= 0 && which < DOMAIN_COUNT) ? g_domains[which].size() : 0;
}

ECL_EXPORT int GetMemoryDomainWritable(int which)
{
	return (which >= 0 && which < DOMAIN_COUNT) ? 1 : 0;
}

/* ---------------------------------------------------------------------------
 * Save data: the memory cards.
 *
 * A PS2 saves to a memory card, which PCSX2 keeps as a file. A sandboxed core
 * has nowhere to put a file that outlives it, and a movie that depended on a
 * card sitting next to the emulator would not replay anywhere else - so the
 * card is memory (patch 0009) and it travels through Chimera's save-data
 * channel: the frontend mounts what the last run left under the card's name,
 * and takes back what this run wrote.
 *
 * Only slots with a card in them are reported. An empty slot has nothing to
 * save, which is different from saving an empty card.
 */
/* What this machine persists, in order: the cards that are actually in a slot,
 * and then the console's own memory. An empty slot has nothing to save, which
 * is not the same as saving an empty card. */
struct SaveData
{
	const char* name;
	const uint8_t* data;
	int64_t size;
};

static int CollectSaveData(SaveData* out, int max)
{
	int count = 0;

	for (int slot = 0; slot < PS2_SLOTS && count < max; slot++)
	{
		size_t size = 0;
		const u8* image = FileMcd_GetImage(static_cast<uint>(slot), &size);
		if (!image)
			continue;
		out[count++] = {MEMCARD_NAME[slot], image, static_cast<int64_t>(size)};
	}

	if (count < max)
	{
		size_t size = 0;
		const u8* nvram = cdvdGetNVRAM(&size);
		if (nvram)
			out[count++] = {NVRAM_NAME, nvram, static_cast<int64_t>(size)};
	}

	return count;
}

#define MAX_SAVEDATA (PS2_SLOTS + 1)

static int SaveDataAt(int32_t i, SaveData* item)
{
	SaveData items[MAX_SAVEDATA];
	const int count = CollectSaveData(items, MAX_SAVEDATA);
	if (i < 0 || i >= count)
		return 0;
	*item = items[i];
	return 1;
}

ECL_EXPORT int32_t GetSaveDataFileCount(void)
{
	SaveData items[MAX_SAVEDATA];
	return CollectSaveData(items, MAX_SAVEDATA);
}

ECL_EXPORT const char* GetSaveDataFileName(int32_t i)
{
	SaveData item;
	return SaveDataAt(i, &item) ? item.name : nullptr;
}

ECL_EXPORT int64_t GetSaveDataFileSize(int32_t i)
{
	SaveData item;
	return SaveDataAt(i, &item) ? item.size : 0;
}

ECL_EXPORT const uint8_t* GetSaveDataFileBuffer(int32_t i)
{
	SaveData item;
	return SaveDataAt(i, &item) ? item.data : nullptr;
}

/* ---------------------------------------------------------------------------
 * The GPU bridge (experiment; see waterbox/gl-bridge.h).
 *
 * The host registers one callback with the sandbox and hands its guest-visible
 * address here BEFORE Init, because the renderer is chosen during Init and the
 * choice depends on whether a GPU is on offer. Without this call - which is
 * every ordinary run - the softpipe draws and nothing here is entered.
 *
 * A machine drawn this way is not the deterministic one. The GPU is outside the
 * sandbox, outside the savestate, and different on every machine; a movie
 * recorded against it carries no promise that it replays. That is the trade the
 * setting names, and it is the frontend's job to say so out loud.
 */
#ifdef CHIMERA_GUEST_GL
ECL_EXPORT void SetGpuBridge(uint64_t addr)
{
	if (!chimera_gl_bridge_start((chimera_gl_bridge_fn_t)addr))
		fprintf(stderr, "chimera: the GPU bridge was offered and refused\n");
}
#endif

} /* extern "C" */
