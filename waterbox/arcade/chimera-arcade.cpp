/* The NAMCO board, wired to the machine.
 *
 * Everything upstream PCSX2 knows about System 246 and 256 goes through this
 * file: the IOP's bus dispatch, the DEV9 bay's three live entry points, and
 * building and taking down the board. The board's own sources sit beside it
 * and know nothing about Chimera; upstream's patches know nothing about the
 * board. That is the whole point of the arrangement - the fork this code comes
 * from rebases onto PCSX2 master daily, and our pin does not.
 *
 * The dispatch below is the fork's, address for address (its IopMem.cpp), with
 * one difference: every path is behind chimera_arcade_present, so a
 * PlayStation 2 project reaches none of it.
 */
#include "chimera-arcade.h"

#include "ACATA.h"
#include "ACATAPI.h"
#include "ACCORE.h"
#include "ACJV.h"
#include "ACRAM.h"
#include "ACSRAM.h"
#include "ACUART.h"

#include "Common.h"
#include "IopDma.h"
#include "IopMem.h"
#include "R3000A.h"
#include "common/Console.h"

#include <cstdlib>
#include <cstring>

int chimera_arcade_present = 0;
int chimera_arcade_verbose = 0;
int chimera_arcade_board = CHIMERA_ARCADE_NONE;

/* ---------------------------------------------------------------------------
 * The bus.
 *
 * The IOP addresses the board through five windows, and the firmware reaches
 * some of them a byte at a time even though they are 16-bit ports - hence the
 * read-modify-write halves below, which are not an accident of the port but
 * what the board's own drivers do.
 */
#define AC_T(mem) ((mem) >> 16)

int ChimeraArcadeRead8(u32 mem, u8* out)
{
	if (!chimera_arcade_present)
		return 0;
	const u32 t = AC_T(mem);
	u16 v;
	if (t == ACSRAM_RANGE)
		v = ACSRAM::Read16(mem);
	else if ((t & 0xFF00) == ACRAM_RANGE)
		v = ACRAM::Read16(mem & ~1u);
	else if ((t & 0xFF00) == ACATA_RANGE)
		v = ACATA::read16(mem & ~1u);
	else if (t == ACJV_RANGE)
		v = ACJV::Read16(mem & ~1u);
	else if (t == 0x1241)
		v = ACCORE::Read16(mem & ~1u);
	else
		return 0;

	/* the SRAM is a byte per port and answers whole; the rest are 16-bit
	 * ports the caller is reading half of */
	*out = (t == ACSRAM_RANGE) ? static_cast<u8>(v)
								: ((mem & 1) ? static_cast<u8>(v >> 8) : static_cast<u8>(v));
	return 1;
}

int ChimeraArcadeRead16(u32 mem, u16* out)
{
	if (!chimera_arcade_present)
		return 0;
	const u32 t = AC_T(mem);
	if (t == 0x1241)
		*out = IS_ACUART_RANGE(mem) ? ACUART::Read16(mem) : ACCORE::Read16(mem);
	else if (t == ACJV_RANGE)
		*out = ACJV::Read16(mem);
	else if (t == ACSRAM_RANGE)
		*out = ACSRAM::Read16(mem);
	else if ((t & 0xFF00) == ACRAM_RANGE)
		*out = ACRAM::Read16(mem);
	else if ((t & 0xFF00) == ACATA_RANGE)
		*out = ACATA::read16(mem);
	else
		return 0;
	return 1;
}

int ChimeraArcadeWrite8(u32 mem, u8 value)
{
	if (!chimera_arcade_present)
		return 0;
	const u32 t = AC_T(mem);
	if ((t & 0xFF00) == ACRAM_RANGE)
	{
		u16 cur = ACRAM::Read16(mem & ~1u);
		cur = (mem & 1) ? ((cur & 0x00FF) | (static_cast<u16>(value) << 8))
						: ((cur & 0xFF00) | value);
		ACRAM::Write16(mem & ~1u, cur);
	}
	else if ((t & 0xFF00) == ACATA_RANGE)
	{
		u16 cur = ACATA::read16(mem & ~1u);
		cur = (mem & 1) ? ((cur & 0x00FF) | (static_cast<u16>(value) << 8))
						: ((cur & 0xFF00) | value);
		ACATA::write16(mem & ~1u, cur);
	}
	else if (t == ACJV_RANGE)
	{
		if (ACJV::enabled)
			ACJV::Write16(mem & ~1u, value);
	}
	else if (t == 0x1241)
	{
		ACCORE::Write16(mem & ~1u, value);
	}
	else
		return 0;
	return 1;
}

int ChimeraArcadeWrite16(u32 mem, u16 value)
{
	if (!chimera_arcade_present)
		return 0;
	const u32 t = AC_T(mem);
	if ((t & 0xFF00) == ACATA_RANGE)
		ACATA::write16(mem, value);
	else if (t == ACJV_RANGE)
	{
		if (ACJV::enabled)
			ACJV::Write16(mem, value);
	}
	else if ((t & 0xFF00) == ACRAM_RANGE)
		ACRAM::Write16(mem, value);
	else if (t == ACSRAM_RANGE)
		ACSRAM::Write16(mem, value);
	else if (t == 0x1241)
	{
		if (IS_ACUART_RANGE(mem))
			ACUART::Write16(mem, value);
		else
			ACCORE::Write16(mem, value);
	}
	else if ((t & 0xFF00) == 0x1300)
		ACCORE::Interrupt(mem, value);
	else
		return 0;
	return 1;
}

int ChimeraArcadeWrite32(u32 mem, u32 value)
{
	if (!chimera_arcade_present)
		return 0;
	if ((AC_T(mem) & 0xFF00) == ACATA_RANGE)
	{
		/* the drive has no 32-bit port; a game that reaches for one is
		 * telling us something, so it is said out loud rather than dropped */
		if (chimera_arcade_verbose)
			Console.Error("ACATA::Write32 %08X: %08X", mem, value);
		return 1;
	}
	return 0;
}

/* ---------------------------------------------------------------------------
 * The DEV9 bay's live entry points. stubs/peripherals.cpp answers the bay with
 * an empty slot for a PlayStation 2 and forwards to these when the board is
 * in it - which is a far smaller answer than compiling upstream's DEV9.cpp and
 * its network stack to reach three functions.
 */
int ChimeraArcadeIrqHandler(void)
{
	return (chimera_arcade_present && ACCORE::hasPendingInterrupt()) ? 1 : 0;
}

void ChimeraArcadeAsync(u32 cycles)
{
	if (!chimera_arcade_present)
		return;
	if (ACUART::s_device)
		ACUART::s_device->Tick(cycles);
	ACJV::UpdateFcaFrame();
}

void ChimeraArcadeReadDMA8Mem(u32* pMem, int size)
{
	if (!chimera_arcade_present)
		return;
	size >>= 1;
	if (ACATAPI::dma_read(pMem, size))
	{
		psxDMA8Interrupt();
	}
	else if (ACCORE::DMA::PendTrasnfType == ACCORE::DMA::ATAPI
		|| ACCORE::DMA::PendTrasnfType == ACCORE::DMA::ATA)
	{
		ACATA::TH::IO_Read(pMem, size);
		ACCORE::DMA::PendTrasnfType = ACCORE::DMA::NONE;
		ACATA::R_STATUS = ATA_STAT_READY;
		ACATA::R_NSECTOR = 0x03;
		psxDMA8Interrupt();
		ACCORE::intr(ACCORE::INTRN_ATA);
	}
	else
	{
		const u32 dma_target = psxHu32(0x1410);
		if ((dma_target & 0xFF000000) == 0x14000000)
		{
			ACRAM::DmaRead(pMem, size, ACRAM::BankFromDmaTarget(dma_target));
			psxDMA8Interrupt();
		}
		else
		{
			if (chimera_arcade_verbose)
				Console.Error("chimera: DMA of %08X bytes with nothing pending (%d)",
					size, static_cast<int>(ACCORE::DMA::PendTrasnfType));
			psxDMA8Interrupt();
		}
	}
}

void ChimeraArcadeWriteDMA8Mem(u32* pMem, int size)
{
	if (!chimera_arcade_present)
		return;
	size >>= 1;
	if (ACCORE::DMA::PendTrasnfType == ACCORE::DMA::ATA_WRITE)
	{
		ACATA::TH::IO_Write(pMem, size);
		ACCORE::DMA::PendTrasnfType = ACCORE::DMA::NONE;
		ACATA::R_STATUS = ATA_STAT_READY;
		psxDMA8Interrupt();
		ACCORE::intr(ACCORE::INTRN_ATA);
		return;
	}
	const u32 dma_target = psxHu32(0x1410);
	if ((dma_target & 0xFF000000) == 0x14000000)
	{
		ACRAM::DmaWrite(pMem, size, ACRAM::BankFromDmaTarget(dma_target));
		psxDMA8Interrupt();
	}
}

/* ---------------------------------------------------------------------------
 * Building the board, and taking it away.
 *
 * The board RAM is allocated HERE rather than being a member of the IOP's
 * memory struct, which is what the fork does. The difference matters at this
 * end of the pipe: a fixed member is 128MB in every machine's address space
 * and in every savestate the greenzone keeps, whether the project is a
 * System 246 or a PlayStation 2 with a disc in it. A System 256 has no board
 * RAM at all and gets none.
 */
static u8* s_acram = nullptr;

const char* ChimeraArcadeInit(int board, u32 acram_bytes)
{
	ChimeraArcadeShutdown();
	if (board == CHIMERA_ARCADE_NONE)
		return nullptr;

	if (acram_bytes > ACRAM_MAX_SIZE)
		return "the board cannot carry that much RAM";
	if (acram_bytes != 0)
	{
		s_acram = static_cast<u8*>(std::calloc(1, acram_bytes));
		if (!s_acram)
			return "there is not enough memory for the board's RAM";
	}
	ACRAM::SetMemory(s_acram, acram_bytes);
	ACSRAM::Clear(0x00);

	chimera_arcade_board = board;
	chimera_arcade_present = 1;

	/* The EE and IOP clocks are the board's, not the console's: a System 256
	 * runs the bus a third faster than a PlayStation 2 and a Super 256 a half
	 * faster. Both counters read these, so they are set before the machine is
	 * built rather than after. */
	switch (board)
	{
		case CHIMERA_ARCADE_256:
			PS2CLK = PS2CLK_S256;
			PSXCLK = PS2CLK_S256 / 8;
			break;
		case CHIMERA_ARCADE_SUPER256:
			PS2CLK = PS2CLK_SS256;
			PSXCLK = PS2CLK_SS256 / 8;
			break;
		default:
			PS2CLK = PS2CLK_DEFAULT;
			PSXCLK = PS2CLK_DEFAULT / 8;
			break;
	}
	return nullptr;
}

void ChimeraArcadeShutdown(void)
{
	chimera_arcade_present = 0;
	chimera_arcade_board = CHIMERA_ARCADE_NONE;
	ACRAM::SetMemory(nullptr, 0);
	if (s_acram)
	{
		std::free(s_acram);
		s_acram = nullptr;
	}
	PS2CLK = PS2CLK_DEFAULT;
	PSXCLK = PS2CLK_DEFAULT / 8;
}

u8* ChimeraArcadeSramBuffer(void) { return ACSRAM::buffer; }
u32 ChimeraArcadeSramSize(void) { return ACSRAM_MAX_SIZE; }
