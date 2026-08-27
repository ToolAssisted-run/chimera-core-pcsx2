/* The peripherals a sandbox has no way to have, and the recorder it does not
 * need.
 *
 * USB and DEV9 are real PS2 hardware - an expansion bay with a network adapter
 * and a hard disk, and two USB ports. Both are OPTIONAL hardware: a stock
 * console has neither populated, and the games this core is for do not look.
 * Emulating them would mean a network stack and a host filesystem inside the
 * sandbox, which is precisely what a sandbox is for not having. The machine
 * still names their entry points, because the IOP's io map does, so they are
 * answered here: reads return the bus's idle value, writes go nowhere, and the
 * console sees an empty bay.
 *
 * InputManager is PCSX2's mapping layer: real joysticks, SDL, keyboards,
 * vibration. A Chimera frontend has already done that work by the time input
 * reaches a core - what arrives here is a button state, not a device - so the
 * layer is absent and Pad's own container (which IS the machine, and is
 * compiled) is driven directly.
 *
 * InputRecording is PCSX2's own movie format. Chimera has one, and a core that
 * kept a second recorder would be recording a machine that two things were
 * driving.
 */
#include "DEV9/DEV9.h"
#include "Input/InputManager.h"
#include "Recording/InputRecording.h"
#include "Recording/InputRecordingControls.h"
#include "Recording/InputRecordingFile.h"
#include "SIO/Multitap/MultitapProtocol.h"
#include "CDVD/CDVDcommon.h"
#include "SIO/Memcard/MemoryCardFile.h"
#include "SIO/Memcard/MemoryCardFolder.h"
#include "USB/USB.h"

#include "fmt/format.h"

/* ---------------------------------------------------------------------------
 * USB: two ports with nothing in them.
 */
void USBinit() {}
void USBasync(u32 cycles) {}
void USBshutdown() {}
void USBclose() {}
bool USBopen() { return true; }
void USBreset() {}
u8 USBread8(u32 addr) { return 0; }
u16 USBread16(u32 addr) { return 0; }
u32 USBread32(u32 addr) { return 0; }
void USBwrite8(u32 addr, u8 value) {}
void USBwrite16(u32 addr, u16 value) {}
void USBwrite32(u32 addr, u32 value) {}

namespace USB
{
	s32 DeviceTypeNameToIndex(const std::string_view device) { return -1; }
	const char* DeviceTypeIndexToName(s32 device) { return "None"; }
	std::string GetConfigSection(int port) { return fmt::format("USB{}", port + 1); }
	void SetDefaultConfiguration(SettingsInterface* si) {}
	void CheckForConfigChanges(const Pcsx2Config& old_config) {}
} // namespace USB

/* ---------------------------------------------------------------------------
 * DEV9: an empty expansion bay. `irqHandler` returning 0 is "nothing to
 * report", which is what a bay with no card in it has.
 */
s32 DEV9init() { return 0; }
void DEV9close() {}
s32 DEV9open() { return 0; }
void DEV9shutdown() {}
int DEV9irqHandler(void) { return 0; }
void DEV9async(u32 cycles) {}
void DEV9writeDMA8Mem(u32* pMem, int size) {}
void DEV9readDMA8Mem(u32* pMem, int size) {}
u8 DEV9read8(u32 addr) { return 0; }
u16 DEV9read16(u32 addr) { return 0; }
u32 DEV9read32(u32 addr) { return 0; }
void DEV9write8(u32 addr, u8 value) {}
void DEV9write16(u32 addr, u16 value) {}
void DEV9write32(u32 addr, u32 value) {}
void DEV9CheckChanges(const Pcsx2Config& old_config) {}

/* ---------------------------------------------------------------------------
 * The mapping layer. The frontend is the mapping layer.
 */
namespace InputManager
{
	void PollSources() {}
	void CloseSources() {}
	void PauseVibration() {}
	void ReloadBindings(SettingsInterface& si, SettingsInterface& binding_si,
		SettingsInterface& hotkey_binding_si, bool is_binding_profile, bool is_hotkey_profile) {}
	void ReloadSources(SettingsInterface& si, std::unique_lock<std::mutex>& settings_lock) {}
	void SetPadVibrationIntensity(u32 pad_index, float large, float small) {}
} // namespace InputManager

/* ---------------------------------------------------------------------------
 * PCSX2's own movie recorder. Chimera records movies; two recorders driving
 * one machine is one too many.
 */
InputRecording g_InputRecording;

bool InputRecording::isActive() const { return false; }
void InputRecording::handleControllerDataUpdate() {}
void InputRecording::handleExceededFrameCounter() {}
void InputRecording::handleLoadingSavestate() {}
void InputRecording::handleReset() {}
void InputRecording::incFrameCounter() {}
void InputRecording::processRecordQueue() {}
void InputRecording::stop() {}
InputRecordingControls& InputRecording::getControls() { return m_controls; }
void InputRecordingControls::processControlQueue() {}
void InputRecordingControls::toggleRecordMode() {}

/* The rest of the mapping layer: chords, hotkeys, which input sources a
 * desktop turns on by default. There are no sources.
 */
namespace InputManager
{
	const char* InputSourceToString(InputSourceType clazz) { return "None"; }
	bool GetInputSourceDefaultEnabled(InputSourceType type) { return false; }
	std::vector<std::string_view> SplitChord(const std::string_view binding) { return {}; }
	std::vector<const HotkeyInfo*> GetHotkeyList() { return {}; }
	GenericInputBindingMapping GetGenericBindingMapping(const std::string_view device) { return {}; }
} // namespace InputManager

bool InputRecordingFile::close() noexcept { return true; }

/* The host's optical drive. A sandbox has no tray, and a movie cannot cite a
 * disc that was in someone else's: a project names an image file, and the file
 * slots carry it. The table is here because CDVD's source switch names it.
 */
const CDVD_API CDVDapi_Disc = {};

FileAccessHelper::FileAccessHelper() = default;
