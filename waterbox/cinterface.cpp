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
static bool g_loaded;

/* the wire: one DualShock 2. Order is the frontend's button order and must
 * match waterbox.config. */
enum
{
	BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT,
	BTN_START, BTN_SELECT,
	BTN_SQUARE, BTN_CROSS, BTN_CIRCLE, BTN_TRIANGLE,
	BTN_L1, BTN_R1, BTN_L2, BTN_R2, BTN_L3, BTN_R3,
	BTN_ANALOG,
	BTN_COUNT
};
static uint8_t g_setButtons[BTN_COUNT];
static uint8_t g_buttons[BTN_COUNT];

/* the analog wire: two sticks, in the frontend's order */
enum { AXIS_LX, AXIS_LY, AXIS_RX, AXIS_RY, AXIS_COUNT };
static int16_t g_axes[AXIS_COUNT];

/* the settings layer host.cpp installs, and the two things the sandbox's own
 * device and audio stream hand back */
SettingsInterface* ChimeraGetSettings();
/* The names the cards travel under. A frontend mounts what the last run left
 * under these names, and stores back what this run produced. */
static const char* const MEMCARD_NAME[2] = {"memcard1.ps2", "memcard2.ps2"};

/* ...and what the CONSOLE remembers when it is switched off: the language, the
 * clock configuration, the region parameters, the machine's iLink id. PCSX2
 * names it after the bios file, and so does this. */
static const char* const NVRAM_NAME = "bios.nvm";

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
static void ApplyInput()
{
	PadBase* pad = Pad::GetPad(0, 0);
	if (!pad)
		return;

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
		pad->Set(map[i].button, g_buttons[map[i].wire] ? 1.0f : 0.0f);

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
	{
		const float value = static_cast<float>(g_axes[sticks[i].axis]) / 32767.0f;
		pad->Set(sticks[i].negative, value < 0.0f ? -value : 0.0f);
		pad->Set(sticks[i].positive, value > 0.0f ? value : 0.0f);
	}
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
		if (!strcmp(value, "opengl"))
			gsRenderer = GSRendererType::OGL;
	}
#endif
	si.SetIntValue("EmuCore/GS", "Renderer", static_cast<int>(gsRenderer));
	si.SetIntValue("EmuCore/GS", "extrathreads", 0);

	/* No deinterlacing.
	 *
	 * A PS2 scans out two fields, and PCSX2's default is "Automatic", which
	 * picks a MOTION ADAPTIVE deinterlacer: it compares this field with the
	 * last two and decides, per pixel, what to show. That is a picture no
	 * movie can promise to reproduce, and it is not what the console put on
	 * the wire. Progressive means the merged frame the GS actually produced,
	 * which is what a frontend should be handed. (It is also what the first
	 * game booted here needed: with the adaptive path the two fields arrived
	 * stacked, one above the other, and every line of text appeared twice.) */
	si.SetIntValue("EmuCore/GS", "deinterlace_mode", static_cast<int>(GSInterlaceMode::Off));
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
	const bool card1 = wbx_setting_bool("memcard1", 1) != 0;
	const bool card2 = wbx_setting_bool("memcard2", 0) != 0;
	si.SetBoolValue("MemoryCards", "Slot1_Enable", card1);
	si.SetBoolValue("MemoryCards", "Slot2_Enable", card2);
	si.SetStringValue("MemoryCards", "Slot1_Filename", MEMCARD_NAME[0]);
	si.SetStringValue("MemoryCards", "Slot2_Filename", MEMCARD_NAME[1]);

	/* No multitap, and so no cards behind one. */
	si.SetBoolValue("Pad", "MultitapPort1", false);
	si.SetBoolValue("Pad", "MultitapPort2", false);
	for (int port = 1; port <= 2; port++)
	{
		for (int slot = 2; slot <= 4; slot++)
			si.SetBoolValue("MemoryCards", fmt::format("Multitap{}_Slot{}_Enable", port, slot).c_str(), false);
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
	 * project says which, and the default is the honest one: the machine
	 * starts where a console starts. */
	si.SetBoolValue("EmuCore", "EnableFastBoot", wbx_setting_bool("fast_boot", 0) != 0);
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
		boot.filename = file;
		boot.source_type = CDVD_SourceType::Iso;
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

ECL_EXPORT void SetAxis(int index, int value)
{
	if (index >= 0 && index < AXIS_COUNT)
		g_axes[index] = static_cast<int16_t>(value);
}

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
	/* two input channels, and a frame is the UNION of them: a controller of
	 * 64 buttons or fewer arrives packed in this call, while the gate harness
	 * (and any wider controller) drives SetButton. */
	for (int i = 0; i < BTN_COUNT; i++)
		g_buttons[i] = g_setButtons[i] || ((packed >> i) & 1);

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

	for (int slot = 0; slot < 2 && count < max; slot++)
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

#define MAX_SAVEDATA 4

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

} /* extern "C" */
