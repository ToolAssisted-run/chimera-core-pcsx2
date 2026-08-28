/* What a sandboxed core refuses, and why.
 *
 * PCSX2 is a whole application: achievements over the network, a fullscreen
 * UI, an on-screen display, video capture, an audio device, input recording,
 * breakpoints, a game-patch database. A Chimera core is a machine and nothing
 * else - the frontend owns everything above it - so none of that is compiled
 * (waterbox/sources.sh says which and why), and what remains are the symbols
 * the machine still names.
 *
 * They are answered here rather than patched out of upstream, because a
 * refusal that lives in this repository is one a pin bump cannot silently
 * undo: if PCSX2 starts calling something new, the link fails and someone
 * decides, instead of a patch quietly not applying.
 *
 * Two of these are refusals on purpose rather than for want of a dependency:
 *
 *   Patch:: is the game-fix database - per-game code changes keyed by disc
 *   serial. A machine that silently rewrites the game's code is not a machine
 *   a movie can be replayed on somewhere else, so a Chimera project states
 *   what it wants and nothing else is applied.
 *
 *   VU_Thread:: is MTVU, which runs a vector unit on a second thread. There is
 *   one thread here, and there would need to be one anyway: a movie cannot
 *   depend on how two threads interleaved.
 */
#include "Achievements.h"
#include "GS/GS.h"
#include "GS/GSCapture.h"
#include "Host/AudioStream.h"
#include "ImGui/FullscreenUI.h"
#include "ImGui/ImGuiManager.h"
#include "MTVU.h"
#include "SIO/Memcard/MemoryCardFile.h"
#include "SIO/Memcard/MemoryCardFolder.h"
#include "Patch.h"
#include "VMManager.h"

#include <mutex>

/* ---------------------------------------------------------------------------
 * Achievements: an online service that also watches memory while a game runs.
 * Neither belongs in a machine a movie must replay identically.
 */
namespace Achievements
{
	bool Initialize() { return true; }
	bool Shutdown(bool allow_cancel) { return true; }
	void ResetClient() {}
	void UpdateSettings(const Pcsx2Config::AchievementsOptions& old_config) {}
	bool ResetHardcoreMode(bool is_booting) { return false; }
	void DisableHardcoreMode() {}
	bool ConfirmSystemReset() { return true; }
	void OnVMPaused(bool paused) {}
	void FrameUpdate() {}
	void IdleUpdate() {}
	void GameChanged(u32 disc_crc, u32 crc) {}
	bool HasActiveGame() { return false; }
	bool HasAchievementsOrLeaderboards() { return false; }
	bool HasRichPresence() { return false; }
	bool IsHardcoreModeActive() { return false; }
	const std::string& GetRichPresenceString()
	{
		static const std::string empty;
		return empty;
	}
	const std::string& GetGameIconURL()
	{
		static const std::string empty;
		return empty;
	}
	std::unique_lock<std::recursive_mutex> GetLock()
	{
		static std::recursive_mutex mutex;
		return std::unique_lock<std::recursive_mutex>(mutex);
	}
} // namespace Achievements

/* ---------------------------------------------------------------------------
 * The screen furniture: a fullscreen UI, an on-screen display, video capture.
 * The frontend draws its own, and a core that drew over the picture would be
 * putting things in a movie that were never in the machine.
 */
namespace FullscreenUI
{
	void CheckForConfigChanges(const Pcsx2Config& old_config) {}
	void GameChanged(std::string title, std::string path, std::string serial, u32 disc_crc, u32 crc) {}
	void OnVMStarted() {}
	void OnVMDestroyed() {}
	void OnVMResumed() {}
	void OpenPauseMenu() {}
	bool OpenAchievementsWindow() { return false; }
	bool OpenLeaderboardsWindow() { return false; }
	void Render() {}
	void ReportStateLoadError(const std::string& filename, std::optional<int> slot, bool was_quick_load) {}
	void ReportStateSaveError(const std::string& filename, std::optional<int> slot) {}
} // namespace FullscreenUI

namespace ImGuiManager
{
	bool Initialize() { return true; }
	void NewFrame() {}
	void SkipFrame() {}
	void RenderOSD() {}
	void WindowResized() {}
	void RequestScaleUpdate() {}
	void ReloadFonts() {}
	void Shutdown(bool clear_state) {}
} // namespace ImGuiManager

namespace GSCapture
{
	bool BeginCapture(float fps, GSVector2i recommended_video_size, float aspect, std::string filename) { return false; }
	void EndCapture() {}
	bool DeliverVideoFrame(GSTexture* stex) { return false; }
	void DeliverAudioPacket(const float* frames) {}
	void Flush() {}
	bool IsCapturing() { return false; }
	bool IsCapturingVideo() { return false; }
	GSVector2i GetSize() { return GSVector2i(0, 0); }
	std::string GetNextCaptureFileName() { return {}; }
	const Threading::ThreadHandle& GetEncoderThreadHandle()
	{
		static Threading::ThreadHandle handle;
		return handle;
	}
} // namespace GSCapture

/* ---------------------------------------------------------------------------
 * The game-patch database: per-game code changes keyed by disc serial. A
 * machine that silently rewrites the game's code is not one a movie replays
 * elsewhere.
 */
namespace Patch
{
	void ApplyPatchSettingOverrides() {}
	bool ReloadPatchAffectingOptions() { return false; }
	void ApplyBootPatches() {}
	void ApplyDynamicPatches(u32 pc) {}
	void ApplyVsyncPatches() {}
} // namespace Patch

/* ---------------------------------------------------------------------------
 * MTVU: a vector unit on a second thread. There is one thread here, and a
 * movie could not depend on how two of them interleaved anyway.
 */
VU_Thread::VU_Thread() = default;
VU_Thread::~VU_Thread() = default;
void VU_Thread::Open() {}
void VU_Thread::Reset() {}
void VU_Thread::WaitVU() {}
void VU_Thread::Get_MTVUChanges() {}
void VU_Thread::ExecuteVU(u32 vu_addr, u32 vif_num, u32 vif_addr, u32 fbits) {}
void VU_Thread::VifUnpack(vifStruct& vif, VIFregisters& vifRegs, const u8* data, u32 size) {}
void VU_Thread::WriteMicroMem(u32 vu_micro_addr, const void* data, u32 size) {}
void VU_Thread::WriteDataMem(u32 vu_data_addr, const void* data, u32 size) {}
void VU_Thread::WriteRow(vifStruct& vif) {}
void VU_Thread::WriteCol(vifStruct& vif) {}

/* The instance the VIF and the microVU name whether or not MTVU is on. It
 * never opens, so nothing is ever handed to it.
 */
VU_Thread vu1Thread;

/* ---------------------------------------------------------------------------
 * The folder memory card: a directory of loose files with a yaml index,
 * presented to the machine as a memory card. A card here is a FILE the
 * save-data channel carries, so this one reports itself absent and the file
 * card (which is compiled) is what a project uses.
 */
FolderMemoryCard::FolderMemoryCard() = default;
void FolderMemoryCard::Open(std::string fullPath, const Pcsx2Config::McdOptions& mcdOptions,
	const u32 sizeInClusters, const bool enableFiltering, std::string filter, bool simulateFileWrites) {}
void FolderMemoryCard::Close(bool flush) {}
bool FolderMemoryCard::IsFormatted() const { return false; }

FolderMemoryCardAggregator::FolderMemoryCardAggregator() = default;
void FolderMemoryCardAggregator::Open() {}
void FolderMemoryCardAggregator::Close() {}
void FolderMemoryCardAggregator::SetFiltering(const bool enableFiltering) {}
s32 FolderMemoryCardAggregator::IsPresent(uint slot) { return 0; }
void FolderMemoryCardAggregator::GetSizeInfo(uint slot, McdSizeInfo& outways) {}
bool FolderMemoryCardAggregator::IsPSX(uint slot) { return false; }
s32 FolderMemoryCardAggregator::Read(uint slot, u8* dest, u32 adr, int size) { return 0; }
s32 FolderMemoryCardAggregator::Save(uint slot, const u8* src, u32 adr, int size) { return 0; }
s32 FolderMemoryCardAggregator::EraseBlock(uint slot, u32 adr) { return 0; }
u64 FolderMemoryCardAggregator::GetCRC(uint slot) { return 0; }
void FolderMemoryCardAggregator::NextFrame(uint slot) {}
bool FolderMemoryCardAggregator::ReIndex(uint slot, const bool enableFiltering, const std::string& filter)
{
	return false;
}

FileAccessHelper::~FileAccessHelper() = default;

#ifdef CHIMERA_GUEST_GL
/* A user's own replacement textures, loaded from PNG and DDS files off disk,
 * and the screenshots the dumper writes back. A project's picture is the
 * machine's; a core has no texture pack directory and nowhere to dump to. The
 * loaders are the only thing in the hardware renderer that wanted libpng. */
#include "GS/Renderers/HW/GSTextureReplacements.h"

GSTextureReplacements::ReplacementTextureLoader GSTextureReplacements::GetLoader(
	const std::string_view filename)
{
	return nullptr;
}

bool GSTextureReplacements::SavePNGImage(const std::string& filename, u32 width, u32 height,
	const u8* buffer, u32 pitch)
{
	return false;
}
#endif
