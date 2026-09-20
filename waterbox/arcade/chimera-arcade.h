/* The NAMCO System 246/256 board, as this core sees it.
 *
 * A System 246 is a PlayStation 2 with a NAMCO expansion board bolted into the
 * DEV9 bay: a JVS I/O board for the cabinet's panel, an ATA/ATAPI drive for
 * the game's media, a UART to a drive board, 32KB of settings SRAM, and up to
 * 128MB of board RAM. The board's own sources live beside this header (they
 * come from PS2Homebrew-arcade/pcsx2x6 and are GPL-3.0+, the licence this
 * package already carries); what upstream PCSX2 has to know about them is only
 * what is declared here, so a pin bump moves a handful of call sites rather
 * than a fork.
 *
 * Everything below answers "no" when the machine is a PlayStation 2. That is
 * the whole of the promise this header makes to the console machines: with
 * chimera_arcade_present at 0 the bus hooks return 0 without reading a thing,
 * and a PS2 project is the machine it always was.
 */
#pragma once

#include "common/Pcsx2Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Which machine this session is. The patched upstream tests this and nothing
 * else - a board that is not there costs one comparison per unclaimed MMIO
 * access, on a path that was already a chain of range tests. */
extern int chimera_arcade_present;

/* What the board says about itself while it works. Upstream's arcade fork puts
 * these behind EmuConfig.Arcade.*; here they are one flag, set from the
 * project's `verbose` setting, because a core's log is the frontend's. */
extern int chimera_arcade_verbose;

/* The three boards, and the console. The value decides the clock, the board
 * RAM and the iLink signature - see ChimeraArcadeInit. */
enum ChimeraArcadeBoard
{
	CHIMERA_ARCADE_NONE = 0,
	CHIMERA_ARCADE_246,
	CHIMERA_ARCADE_256,
	CHIMERA_ARCADE_SUPER256,
};

extern int chimera_arcade_board;

/* The board's MMIO, on the IOP's bus. Each returns 1 when the address belonged
 * to the board and 0 when it did not, so upstream's dispatch falls through to
 * what it always did. */
int ChimeraArcadeRead8(u32 addr, u8* out);
int ChimeraArcadeRead16(u32 addr, u16* out);
int ChimeraArcadeWrite8(u32 addr, u8 val);
int ChimeraArcadeWrite16(u32 addr, u16 val);
int ChimeraArcadeWrite32(u32 addr, u32 val);

/* Build the board for this machine, or take it away again. acram_bytes is how
 * much board RAM the rack carries: a System 246 rack has 32, 64 or 128MB on
 * expansion PCBs, a System 256 has none. It is allocated here and only here,
 * so a PlayStation 2 project pays nothing for a board it has not got - which
 * is the one place this core deliberately improves on the fork, where the
 * maximum 128MB is a member of the IOP's memory struct and so is allocated,
 * savestated and carried by every machine.
 *
 * Returns 0 on success, or a message on failure (the caller owns nothing). */
const char* ChimeraArcadeInit(int board, u32 acram_bytes);
void ChimeraArcadeShutdown(void);

/* The DEV9 bay's three live entry points, which stubs/peripherals.cpp
 * forwards to when a board is present: the pending interrupt, and the two
 * halves of the ATA/ATAPI DMA. */
int ChimeraArcadeIrqHandler(void);
void ChimeraArcadeAsync(u32 cycles);
void ChimeraArcadeReadDMA8Mem(u32* pMem, int size);
void ChimeraArcadeWriteDMA8Mem(u32* pMem, int size);

/* The board's 32KB settings memory, as bytes the save-data channel carries.
 * The fork keeps it in a file beside the game; a movie cannot cite a file on
 * somebody's disk, so here it is a buffer the frontend mounts and exports. */
u8* ChimeraArcadeSramBuffer(void);
u32 ChimeraArcadeSramSize(void);

#ifdef __cplusplus
}
#endif
