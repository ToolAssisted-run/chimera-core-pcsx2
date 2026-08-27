/* What PCSX2 asks of the application around it, answered by a core.
 *
 * PCSX2's core is written against a host: something that owns a window, holds
 * the settings, shows messages, runs work on the right thread and decides when
 * the machine starts and stops. Its own front ends (the Qt application, the
 * headless GS runner) each implement this interface, and so does this file -
 * for a host that is a SANDBOX inside a frontend, which changes most of the
 * answers.
 *
 * Three principles, and every function here follows one of them:
 *
 *   The frontend already owns it. Messages, windows, fullscreen, the game
 *   list, the clipboard: Chimera has all of that, and a core reaching for it
 *   would be a second opinion nobody asked for. These do nothing.
 *
 *   The settings are the project's. PCSX2 expects an ini file it can read and
 *   write; a Chimera project states its settings, and they arrive through the
 *   package. So the settings interface is backed by memory that nothing
 *   persists, and writes to it are dropped rather than saved - a core that
 *   could change its own settings between runs is a core whose movies do not
 *   replay.
 *
 *   There is one thread. RunOnCPUThread and RunOnGSThread call their function
 *   where they stand, because there is nowhere else for it to run.
 */
#include "Host.h"
#include "GS/GS.h"
#include "Host/AudioStream.h"
#include "VMManager.h"
#include "common/Console.h"
#include "common/SettingsInterface.h"
#include "common/MemorySettingsInterface.h"
#include "common/SettingsWrapper.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <type_traits>

/* The settings the machine runs with. A Chimera project decides these before
 * the machine exists, and there is no ini file: this is PCSX2's own in-memory
 * settings interface, installed as the base layer. Its accessors are upstream's
 * (pcsx2/Host.cpp), so a core does not get to answer "what is this setting"
 * differently from the machine that reads it.
 *
 * Nothing persists it. A core that could change its own settings between runs
 * is a core whose movies do not replay.
 */
static MemorySettingsInterface s_settings;

SettingsInterface* ChimeraGetSettings()
{
	static bool installed = false;
	if (!installed)
	{
		Host::Internal::SetBaseSettingsLayer(&s_settings);
		installed = true;
	}
	return &s_settings;
}

/* Nothing is written out: a core that could change its own settings between
 * runs is a core whose movies do not replay. */
void Host::CommitBaseSettingChanges() {}
void Host::LoadSettings(SettingsInterface& si, std::unique_lock<std::mutex>& lock) {}
void Host::CheckForSettingsChanges(const Pcsx2Config& old_config) {}
void Host::SetDefaultUISettings(SettingsInterface& si) {}
bool Host::RequestResetSettings(bool folders, bool core, bool controllers, bool hotkeys, bool ui) { return false; }

/* ---------------------------------------------------------------------------
 * Messages. The frontend has a screen; a core has stderr, which is where a
 * gate and a bug report can both find it.
 */
void Host::AddOSDMessage(std::string message, float duration) { Console.WriteLn("%s", message.c_str()); }
void Host::AddKeyedOSDMessage(std::string key, std::string message, float duration) { Console.WriteLn("%s", message.c_str()); }
void Host::AddIconOSDMessage(std::string key, const char* icon, const std::string_view message, float duration)
{
	Console.WriteLn("%.*s", static_cast<int>(message.size()), message.data());
}
void Host::RemoveKeyedOSDMessage(std::string key) {}
void Host::ClearOSDMessages() {}

void Host::ReportInfoAsync(const std::string_view title, const std::string_view message)
{
	std::fprintf(stderr, "[pcsx2] %.*s: %.*s\n", static_cast<int>(title.size()), title.data(),
		static_cast<int>(message.size()), message.data());
}
void Host::ReportErrorAsync(const std::string_view title, const std::string_view message)
{
	std::fprintf(stderr, "[pcsx2] ERROR %.*s: %.*s\n", static_cast<int>(title.size()), title.data(),
		static_cast<int>(message.size()), message.data());
}
void Host::OpenURL(const std::string_view url) {}
bool Host::CopyTextToClipboard(const std::string_view text) { return false; }
std::string Host::GetTextFromClipboard() { return {}; }

/* Translation: a core speaks one language, and the frontend does the rest. The
 * string comes back unchanged, which is what an untranslated build is. */
s32 Host::Internal::GetTranslatedStringImpl(const std::string_view context, const std::string_view msg,
	char* tbuf, size_t tbuf_space)
{
	if (msg.size() > tbuf_space)
		return -1;
	std::memcpy(tbuf, msg.data(), msg.size());
	return static_cast<s32>(msg.size());
}

/* ---------------------------------------------------------------------------
 * The window, which does not exist. A sandboxed core hands its picture to the
 * ABI; there is nothing to acquire, resize, or make fullscreen.
 */
/* There is no window. The GS renders into memory and the ABI hands that memory
 * to the frontend, so the "surface" is a description of nothing. */
std::optional<WindowInfo> Host::AcquireRenderWindow(bool recreate_window)
{
	WindowInfo wi;
	wi.type = WindowInfo::Type::Surfaceless;
	wi.surface_width = 640;
	wi.surface_height = 448;
	return wi;
}
void Host::ReleaseRenderWindow() {}
void Host::BeginPresentFrame() {}
bool Host::IsFullscreen() { return false; }
void Host::SetFullscreen(bool enabled) {}
void Host::RequestResizeHostDisplay(s32 width, s32 height) {}
void Host::RequestVMShutdown(bool allow_confirm, bool allow_save_state, bool default_save_state) {}

/* ---------------------------------------------------------------------------
 * Threads, of which there is one. Both of these run their work where they
 * stand, because there is nowhere else for it to go.
 */
void Host::RunOnCPUThread(std::function<void()> function, bool block) { function(); }
void Host::RunOnGSThread(std::function<void()> function) { function(); }
void Host::PumpMessagesOnCPUThread() {}

/* ---------------------------------------------------------------------------
 * The lifecycle. The frontend knows all of this already - it is the one asking
 * for the frames.
 */
void Host::OnVMStarting() {}
void Host::OnVMStarted() {}
void Host::OnVMDestroyed() {}
void Host::OnVMPaused() {}
void Host::OnVMResumed() {}
void Host::OnGameChanged(const std::string& title, const std::string& elf_override, const std::string& disc_path,
	const std::string& disc_serial, u32 disc_crc, u32 current_crc) {}
void Host::OnPerformanceMetricsUpdated() {}
void Host::OnSaveStateLoading(const std::string_view filename) {}
void Host::OnSaveStateLoaded(const std::string_view filename, bool was_successful) {}
void Host::OnSaveStateSaved(const std::string_view filename) {}

void Host::RefreshGameListAsync(bool invalidate_cache) {}
void Host::CancelGameListRefresh() {}

bool Host::InBatchMode() { return true; }
bool Host::InNoGUIMode() { return true; }
