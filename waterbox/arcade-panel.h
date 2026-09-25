/* The JVS switch words the arcade panel's lines make, player by player.
 *
 * Two of the panel's groups drive the SAME bits of player 1's word. The
 * standard lines are the board's own wiring (up, down, left, right, buttons
 * 1 to 6, start, service); Zoids' twin levers, triggers and buttons are wired
 * to switch bits of their own, and twelve of those are bits the standard
 * lines use too - Zoids reads its panel through the same word.
 *
 * Until 2026-09-25 the two groups were applied one after the other, each bit
 * set or CLEARED by its line, so the twin group - nothing held on it - wrote
 * player 1's down, left, right and buttons 1 to 6 back to released on every
 * frame. Every cabinet lost them: Time Crisis 4 fires with P1 Left and never
 * could (chimera issue 146), and the fighting panel's buttons did nothing
 * unless bound to a lever line that happens to share the bit (issue 132).
 * A bit is pressed when ANY line that drives it is held; this is that OR, in
 * one place, so the machine-free test below can hold it.
 */
#pragma once

#include <cstdint>

namespace arcade_panel {

/* The JVS switch-word bit each standard line is: the board's own wiring
 * (waterbox/arcade/ACJV.h). Twelve per player, the same twelve for both. */
constexpr uint16_t kPlayerBits[12] = {
	0x20, 0x10, 0x08, 0x04,          /* up, down, left, right */
	0x02, 0x01, 0x8000, 0x4000,      /* buttons 1 to 4 */
	0x2000, 0x1000,                  /* buttons 5 and 6 */
	0x80, 0x40,                      /* start, service */
};

/* Zoids' twin levers, triggers and buttons: switch-word bits of player 1. */
constexpr uint16_t kTwinBits[12] = {
	0x0001, 0x8000, 0x4000, 0x2000,  /* left lever  up, down, left, right */
	0x0010, 0x0008, 0x0004, 0x0002,  /* right lever up, down, left, right */
	0x0400, 0x1000,                  /* left and right trigger */
	0x0200, 0x0800,                  /* left and right button  */
};

struct Words
{
	uint16_t pressed[2];  /* the bits held down */
	uint16_t driven[2];   /* every bit some line drives, held or not */
};

/* p1/p2: the twelve standard lines of each player, in kPlayerBits order;
 * twin: the twelve twin-stick lines, in kTwinBits order. Nonzero = held. */
inline Words Compute(const uint8_t *p1, const uint8_t *p2, const uint8_t *twin)
{
	Words w = {{0, 0}, {0, 0}};
	for (int i = 0; i < 12; i++)
	{
		w.driven[0] |= kPlayerBits[i];
		w.driven[1] |= kPlayerBits[i];
		w.driven[0] |= kTwinBits[i];
		if (p1[i])
			w.pressed[0] |= kPlayerBits[i];
		if (p2[i])
			w.pressed[1] |= kPlayerBits[i];
		if (twin[i])
			w.pressed[0] |= kTwinBits[i];
	}
	return w;
}

} // namespace arcade_panel
