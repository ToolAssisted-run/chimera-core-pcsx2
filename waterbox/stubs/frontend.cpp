/* What belongs to a frontend, answered so the machine links.
 *
 * PCSX2 the application keeps a game list, a game database of per-title fixes,
 * a save-state slot UI, an on-screen font, replacement textures loaded from a
 * directory, and an audio device. Chimera owns every one of those, or refuses
 * them on purpose:
 *
 *   The GAME DATABASE would silently change the machine's configuration - and
 *   sometimes the game's code - based on a disc serial. A Chimera project
 *   states what it wants; a movie recorded on this core replays on the
 *   settings the movie carries, not on whatever the database said that week.
 *
 *   SAVESTATES are the sandbox's own: it snapshots the whole guest, which is
 *   both smaller and more honest than a format that has to know every field.
 *   PCSX2's zip-to-disk path is therefore refused rather than reimplemented.
 *
 *   REPLACEMENT TEXTURES are content from outside the machine, and this core
 *   has no hardware renderer to apply them to (patch 0002).
 *
 *   The AUDIO DEVICE is the frontend, which is its own file
 *   (waterbox/audio-stream.cpp).
 */
#include "GS/GS.h"
#include "GS/GSPng.h"
#include "common/Image.h"
#include "imgui_freetype.h"
#include "imgui.h"
#include "GS/Renderers/HW/GSTextureCache.h"
#include "GS/Renderers/HW/GSTextureReplacements.h"
#include "GameDatabase.h"
#include "GameList.h"
#include "Host.h"
#include "Host/AudioStream.h"
#include "ImGui/ImGuiManager.h"
#include "ImGui/ImGuiOverlays.h"
#include "Patch.h"
#include "SaveState.h"
#include "common/HostSys.h"
#include "common/ProgressCallback.h"

/* ---------------------------------------------------------------------------
 * The game list and the per-title database.
 */
namespace GameList
{
	std::unique_lock<std::recursive_mutex> GetLock()
	{
		static std::recursive_mutex mutex;
		return std::unique_lock<std::recursive_mutex>(mutex);
	}
	const Entry* GetEntryByCRC(u32 crc) { return nullptr; }
	bool GetSerialAndCRCForFilename(const char* filename, std::string* serial, u32* crc) { return false; }
	void AddPlayedTimeForSerial(const std::string& serial, std::time_t last_time, std::time_t add_time) {}
	std::string GetCustomTitleForPath(const std::string& path) { return {}; }
} // namespace GameList

namespace GameDatabase
{
	const GameDatabaseSchema::GameEntry* findGame(const std::string_view serial) { return nullptr; }
} // namespace GameDatabase

void GameDatabaseSchema::GameEntry::applyGameFixes(Pcsx2Config& config, bool applyAuto) const {}
void GameDatabaseSchema::GameEntry::applyGSHardwareFixes(Pcsx2Config::GSOptions& config) const {}
std::string GameDatabaseSchema::GameEntry::memcardFiltersAsString() const { return {}; }

namespace Patch
{
	void ReloadPatches(const std::string& serial, u32 crc, bool force_reload_files, bool reload_enabled_list,
		bool verbose, bool verbose_if_changed) {}
	void UpdateActivePatches(bool reload_enabled_list, bool verbose, bool verbose_if_changed, bool apply_widescreen) {}
	void UnloadPatches() {}
} // namespace Patch

/* ---------------------------------------------------------------------------
 * Savestates. The sandbox snapshots the guest; PCSX2's own format is not
 * compiled, and the paths that would write one say so by failing.
 */
bool SaveStateBase::FreezeTag(const char* src) { return true; }
std::unique_ptr<ArchiveEntryList> SaveState_DownloadState(Error* error) { return nullptr; }
std::unique_ptr<SaveStateScreenshotData> SaveState_SaveScreenshot() { return nullptr; }
bool SaveState_ZipToDisk(std::unique_ptr<ArchiveEntryList> srclist,
	std::unique_ptr<SaveStateScreenshotData> screenshot, const char* filename, Error* error)
{
	return false;
}
bool SaveState_UnzipFromDisk(const std::string& filename, Error* error) { return false; }
void SaveState_ReportLoadErrorOSD(const std::string& message, std::optional<s32> slot, bool backup) {}
void SaveState_ReportSaveErrorOSD(const std::string& message, std::optional<s32> slot) {}

namespace SaveStateSelectorUI
{
	void Clear() {}
	void SelectNextSlot(bool open_selector) {}
	void SelectPreviousSlot(bool open_selector) {}
	void LoadCurrentSlot() {}
	void LoadCurrentBackupSlot() {}
	void SaveCurrentSlot() {}
} // namespace SaveStateSelectorUI

/* ---------------------------------------------------------------------------
 * The screen furniture the GS still names: a font, a scale, an overlay
 * position. Nothing draws.
 */
namespace ImGuiManager
{
	ImFont* GetFixedFont() { return nullptr; }
	float GetFontSizeStandard() { return 15.0f; }
	float GetGlobalScale() { return 1.0f; }
	float GetWindowWidth() { return 0.0f; }
} // namespace ImGuiManager

ImVec2 CalculatePerformanceOverlayTextPosition(OsdOverlayPos position, float margin, const ImVec2& text_size,
	float window_width, float position_y)
{
	return ImVec2(0.0f, 0.0f);
}

/* ---------------------------------------------------------------------------
 * Replacement textures, and the hardware texture cache they live in. Without
 * a guest Mesa there is no hardware renderer to apply either to (patch 0002),
 * so they are answered here; with one, the real ones are compiled.
 */
#ifndef CHIMERA_GUEST_GL
std::unique_ptr<GSTextureCache> g_texture_cache;

namespace GSTextureReplacements
{
	void GameChanged() {}
	void ReloadReplacementMap() {}
	void UpdateConfig(Pcsx2Config::GSOptions& old_config) {}
	void Shutdown() {}
} // namespace GSTextureReplacements
#endif

/* ---------------------------------------------------------------------------
 * The last few things a desktop has and a sandbox does not.
 */
namespace Host
{
	std::unique_ptr<ProgressCallback> CreateHostProgressCallback()
	{
		return ProgressCallback::CreateNullProgressCallback();
	}
	void SetMouseLock(bool enabled) {}
	std::string TranslatePluralToString(const char* context, const char* msg, const char* disambiguation, int count)
	{
		return fmt::format(fmt::runtime(msg), count);
	}
} // namespace Host

namespace Common
{
	bool InhibitScreensaver(bool inhibit) { return true; }
} // namespace Common

/* The picture leaves through the ABI as pixels, so nothing here writes an
 * image file: not a screenshot, not a png dump of a texture.
 */
RGBA8Image::RGBA8Image() = default;
RGBA8Image::RGBA8Image(RGBA8Image&& move) = default;
bool RGBA8Image::SaveToFile(const char* filename, u8 quality) const { return false; }

namespace GSPng
{
	bool Save(GSPng::Format fmt, const std::string& file, const u8* image, int w, int h, int pitch,
		int compression, bool rb_swapped)
	{
		return false;
	}
} // namespace GSPng

/* The hardware texture cache: declared because the pointer above is, destroyed
 * because a unique_ptr wants to know how. It is never constructed.
 */
#ifndef CHIMERA_GUEST_GL
GSTextureCache::~GSTextureCache() = default;
#endif

namespace ImGuiFreeType
{
	const ImFontLoader* GetFontLoader() { return nullptr; }
} // namespace ImGuiFreeType

/* Which version of PCSX2 this is. A movie cites the CORE PACKAGE's version
 * (build.json, stamped from the commit), so what upstream would put here is
 * the pinned submodule and nothing more.
 */
namespace BuildVersion
{
	const char* GitRev = "chimera";
} // namespace BuildVersion
