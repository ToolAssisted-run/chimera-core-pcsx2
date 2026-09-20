#pragma once

#include "MemoryTypes.h"
#include "chimera-arcade.h"
#include "common/Pcsx2Types.h"
#include "common/Pcsx2Defs.h"
#include <string>

#define ACSRAM_ADDR_BASE 0x12500000
#define ACSRAM_RANGE     0x1250
#define ACSRAM_MAX_SIZE  _32kb // size of the SRAM

#define ACSRAM_LOG(fmt, ...) if (chimera_arcade_verbose) Console.WriteLn(Color_Gray, "SRAM:" fmt __VA_OPT__(,) __VA_ARGS__)

namespace ACSRAM
{
    // dont ask me why... but for some reason, ACSRAM reads may be detected as 8bit MMIO
    // yet the address increments as if it was 16bit.
    // homebrew ACSRAM does not exhibit this behavior: it goes over 16bit mmio as the rest of the IRXes
    u8 Read8(u32 addr);
    u16 Read16(u32 addr);
    void Write16(u32 addr, u16 val);

    // data
    extern u8 buffer[];

    /* chimera: the board's settings memory is a BUFFER, not a file beside the
     * game. The frontend mounts what a project starts from and exports what a
     * run leaves behind (waterbox/cinterface.cpp), which is what makes a movie
     * that starts from a cabinet somebody set up in the test menu replayable
     * anywhere. Upstream's ReadFile/WriteFile are gone with the file. */
    void Clear(u8 fillerbyte = 0x0);
}
