#include "ACSRAM.h"
#include "IopMem.h"
#include "common/Console.h"
#include "ACMACROS.h"
#include <cstring>

u8 ACSRAM::buffer[ACSRAM_MAX_SIZE];

#define OOB_REPORT(T) Console.Error("%s: out of bound index: %08X", __FUNCTION__, T);
#define GET_SRAM_OFF(t) ((t - ACSRAM_ADDR_BASE)/2) // u8 buffer on u16 MMIO, halve the address to get real offset

void ACSRAM::Clear(u8 fillerbyte) {
    std::memset(ACSRAM::buffer, fillerbyte, sizeof(ACSRAM::buffer));
}

u8 ACSRAM::Read8(u32 addr) {
    u32 T = GET_SRAM_OFF(addr);
    if (T < ACSRAM_MAX_SIZE) {
        ACSRAM_LOG("read8  [%04X]:%02X", T, ACSRAM::buffer[T]);
        return ACSRAM::buffer[T];
    } else OOB_REPORT(T);
    return 0;
}

u16 ACSRAM::Read16(u32 addr) {
    u32 T = GET_SRAM_OFF(addr);
    if (T < ACSRAM_MAX_SIZE) {
        ACSRAM_LOG("read16 [%04X]:%02X", T, ACSRAM::buffer[T]);
        return ACSRAM::buffer[T];
    } else OOB_REPORT(T);
    return 0;
}

void ACSRAM::Write16(u32 addr, u16 val) {
    u32 T = GET_SRAM_OFF(addr);
    if (T < ACSRAM_MAX_SIZE) {
        ACSRAM_LOG("write16 [%04X]=%02X", T, val);
        ACSRAM::buffer[T] = val;
    } else OOB_REPORT(T);
}
