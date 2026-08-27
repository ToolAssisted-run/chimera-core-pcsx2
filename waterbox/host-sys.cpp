/* What the machine is allowed to know about the computer it is running on:
 * nothing.
 *
 * PCSX2's platform layer (common/Linux/LnxMisc.cpp) answers these by asking
 * the host - the wall clock, the invariant TSC frequency, how much memory is
 * installed, which CPU this is, how many cores it has. Every one of those is a
 * number that differs between two machines, and a core that reads any of them
 * is a core whose movies replay differently on someone else's computer. So the
 * platform layer is not compiled (waterbox/sources.sh) and the answers are
 * pinned here.
 *
 * The clock is the important one, and the first version of it was wrong in a
 * way worth recording: it advanced one tick per READ, which sounds
 * deterministic and is not. Two builds that read it a different number of
 * times - because one logged more, say - see two different clocks, and the
 * machine diverged after about forty frames while every other channel still
 * matched.
 *
 * So the clock belongs to the MACHINE: it advances one frame's worth of
 * microseconds every time the frontend asks for a frame, and not otherwise.
 * Reading it has no effect on it. Two runs that produce the same frames see
 * the same clock, whatever either build did in between.
 *
 * The CPU description is pinned for the same reason. cpuinfo IS vendored in
 * the PCSX2 tree and would build, but what it reports is whatever CPUID says
 * on the machine that ran it, and PCSX2 picks its vector ISA from that. A
 * fixed answer (SSE4, one core) makes that choice a property of the build
 * rather than of the computer.
 */
#include "common/Console.h"
#include "common/HostSys.h"
#include "common/Threading.h"

#include "cpuinfo.h"

#include <csignal>
#include <ctime>

/* ---------------------------------------------------------------------------
 * A clock that is the same everywhere. One tick is one microsecond, and one
 * read is one tick.
 */
static u64 s_ticks = 0;

/* Called by cinterface.cpp at the top of every frame. One NTSC frame is
 * 16683 microseconds; the exact figure does not matter, only that it is the
 * same figure on both sides of the gate and that it never moves on its own. */
extern "C" void ChimeraAdvanceClock(void)
{
	s_ticks += 16683;
}

u64 GetTickFrequency()
{
	return 1000000;
}

u64 GetCPUTicks()
{
	return s_ticks;
}

std::string GetOSVersionString()
{
	return "Chimera waterbox";
}

/* Sleeping is what a thread does while another one works. There is one thread
 * here, so a sleep that returned late would only be a slower frame.
 */
namespace Threading
{
	void Sleep(int ms) {}
	void SleepUntil(u64 ticks) {}
} // namespace Threading

/* The sandbox's heap is the machine's memory, and its size is in
 * waterbox.config rather than in whatever this computer happens to have. The
 * figures below are what PCSX2 logs and, in one place, sizes a cache from.
 */
size_t GetPhysicalMemory()
{
	return static_cast<size_t>(2048) * 1024 * 1024;
}

size_t GetAvailablePhysicalMemory()
{
	return static_cast<size_t>(1024) * 1024 * 1024;
}

/* A crash handler installs signal handlers and writes a minidump. The sandbox
 * is the crash handler: a guest that faults takes the guest down and the
 * frontend says so.
 */
namespace CrashHandler
{
	void CrashSignalHandler(int signal, siginfo_t* siginfo, void* ctx) {}
} // namespace CrashHandler

/* ---------------------------------------------------------------------------
 * The pinned CPU: one core, one processor, SSE4 and no more. cpuinfo's own
 * accessors are inline over these, so the whole library reduces to a zeroed
 * feature struct and a handful of empty descriptions.
 */
extern "C"
{
	struct cpuinfo_x86_isa cpuinfo_isa = {};

	static const struct cpuinfo_processor s_processor = {};
	static const struct cpuinfo_core s_core = {};
	static const struct cpuinfo_cluster s_cluster = {};
	static const struct cpuinfo_package s_package = {};
	static const struct cpuinfo_uarch_info s_uarch = {};

	bool cpuinfo_initialize(void) { return true; }
	uint32_t cpuinfo_get_processors_count(void) { return 1; }
	uint32_t cpuinfo_get_cores_count(void) { return 1; }
	uint32_t cpuinfo_get_clusters_count(void) { return 1; }
	uint32_t cpuinfo_get_uarchs_count(void) { return 1; }
	const struct cpuinfo_processor* cpuinfo_get_processor(uint32_t index) { return &s_processor; }
	const struct cpuinfo_core* cpuinfo_get_core(uint32_t index) { return &s_core; }
	const struct cpuinfo_cluster* cpuinfo_get_clusters(void) { return &s_cluster; }
	const struct cpuinfo_package* cpuinfo_get_package(uint32_t index) { return &s_package; }
	const struct cpuinfo_uarch_info* cpuinfo_get_uarch(uint32_t index) { return &s_uarch; }
}
