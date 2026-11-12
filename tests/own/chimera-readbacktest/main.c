/* chimera-readbacktest - a PlayStation 2 test program of the PCSX2 core's,
 * for what a game reads back of what it drew (chimera issue 560).
 *
 * Every frame it clears the picture, draws a 64x64 square whose colour
 * changes with the frame count and a line that moves a pixel a frame, and
 * then copies the square, GS to GS, into memory past the picture as 16-bit
 * pixels. The graphics card cannot change a pixel's size in a copy, so
 * PCSX2's hardware renderers read the square back from the card for it,
 * through their texture cache's download texture, every frame. A renderer
 * that kept that texture across a savestate load as the old device's shows
 * it on the first frame after one.
 *
 * It uses nothing but the GS: no pads, no files, no IOP modules, so it runs
 * on a retail BIOS and on OpenPS2BIOS alike.
 *
 * SPDX-License-Identifier: MIT
 */
#include <kernel.h>
#include <gsKit.h>
#include <gsInline.h>
#include <dmaKit.h>

void _libcglue_init(void) { }
void _libcglue_deinit(void) { }
DISABLE_PATCHED_FUNCTIONS();

/* the square at the picture's top left corner, to `to` (a byte address in
 * the GS's memory) as 16-bit pixels: a local-to-local transfer */
static void copy_square(GSGLOBAL *gs, unsigned to)
{
	const int registers = 4;
	u64 *p_store = gsKit_heap_alloc(gs, registers, registers * 16, GIF_AD);
	u64 *p_data = p_store;
	if (p_store == gs->CurQueue->last_tag) {
		*p_data++ = GIF_TAG_AD(registers);
		*p_data++ = GIF_AD;
	}
	*p_data++ = GS_SETREG_BITBLTBUF(gs->ScreenBuffer[0] / 256, gs->Width / 64, gs->PSM, to / 256, gs->Width / 64, GS_PSM_CT16);
	*p_data++ = GS_BITBLTBUF;
	*p_data++ = GS_SETREG_TRXPOS(0, 0, 0, 0, 0);
	*p_data++ = GS_TRXPOS;
	*p_data++ = GS_SETREG_TRXREG(64, 64);
	*p_data++ = GS_TRXREG;
	*p_data++ = GS_SETREG_TRXDIR(2);
	*p_data++ = GS_TRXDIR;
}

int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	GSGLOBAL *gs = gsKit_init_global();
	gs->DoubleBuffering = GS_SETTING_OFF;
	gs->ZBuffering = GS_SETTING_OFF;
	dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
	dmaKit_chan_init(DMA_CHANNEL_GIF);
	gs->PrimAlphaEnable = GS_SETTING_OFF;
	gsKit_init_screen(gs);
	gsKit_mode_switch(gs, GS_ONESHOT);
	const unsigned spare = gsKit_vram_alloc(gs, gsKit_texture_size(gs->Width, 64, GS_PSM_CT16), GSKIT_ALLOC_USERBUFFER);

	const u64 back = GS_SETREG_RGBAQ(0x10, 0x18, 0x30, 0x80, 0x00);
	unsigned frame = 0;
	for (;;) {
		const u64 colour = GS_SETREG_RGBAQ(frame & 0xff, (frame * 3) & 0xff, 0x80, 0x80, 0x00);
		const unsigned at = frame % 512;
		gsKit_clear(gs, back);
		gsKit_prim_sprite(gs, 0, 0, 64, 64, 1, colour);
		gsKit_prim_sprite(gs, 40 + at, 400, 41 + at, 416, 1, colour);
		copy_square(gs, spare);
		gsKit_queue_exec(gs);
		gsKit_sync_flip(gs);
		frame++;
	}
	return 0;
}
