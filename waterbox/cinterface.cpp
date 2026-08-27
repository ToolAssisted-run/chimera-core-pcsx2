/* cinterface.cpp - PCSX2 behind the chimera guest ABI.
 *
 * M1: this file exists to make the machine BUILD and INITIALISE. A PS2 has no
 * HLE bios, so nothing here can run until a project supplies one, and the
 * gates say so rather than pretending otherwise (docs/PLAN.md).
 */
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <emulibc.h>
#include <waterbox_settings.h>
#include <waterbox_slots.h>

static char g_loadError[512];

extern "C" {

ECL_EXPORT const char *GetLoadError(void) { return g_loadError; }

ECL_EXPORT int Init(void)
{
	snprintf(g_loadError, sizeof(g_loadError), "not implemented yet");
	return 0;
}

} /* extern "C" */
