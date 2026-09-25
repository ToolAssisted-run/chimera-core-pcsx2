// The arcade panel's switch words, with no machine (chimera issues 132, 146).
//
// Built and run by waterbox/run-gate.sh. Each case holds some lines and checks
// the bits the JVS board is then handed. The last case replays the order the
// core used to apply the two groups in - standard lines, then twin-stick lines
// each setting OR CLEARING its bit - and requires it to fail, so this test
// cannot go blind to the bug it exists for.
#include "arcade-panel.h"

#include <cstdio>
#include <cstring>

static int failures = 0;

static void expect(const char *what, unsigned got, unsigned want)
{
	if (got != want)
	{
		std::printf("FAIL %s: got %#06x, want %#06x\n", what, got, want);
		failures++;
	}
}

struct Lines
{
	uint8_t p1[12], p2[12], twin[12];
	Lines() { std::memset(this, 0, sizeof *this); }
};

enum { UP, DOWN, LEFT, RIGHT, B1, B2, B3, B4, B5, B6, START, SERVICE };
enum { RLEVER_DOWN = 5 };

// The order the core used before 2026-09-25: each group sets or clears.
static uint16_t OldPlayerOne(const Lines &l)
{
	uint16_t word = 0;
	for (int i = 0; i < 12; i++)
		word = l.p1[i] ? (word | arcade_panel::kPlayerBits[i]) : (word & ~arcade_panel::kPlayerBits[i]);
	for (int i = 0; i < 12; i++)
		word = l.twin[i] ? (word | arcade_panel::kTwinBits[i]) : (word & ~arcade_panel::kTwinBits[i]);
	return word;
}

int main()
{
	{
		Lines l;
		arcade_panel::Words w = arcade_panel::Compute(l.p1, l.p2, l.twin);
		expect("nothing held, P1", w.pressed[0], 0);
		expect("nothing held, P2", w.pressed[1], 0);
	}
	{
		// Time Crisis 4's trigger is the board's P1 LEFT bit.
		Lines l;
		l.p1[LEFT] = 1;
		arcade_panel::Words w = arcade_panel::Compute(l.p1, l.p2, l.twin);
		expect("P1 Left fires Time Crisis 4", w.pressed[0], 0x08);
		expect("P1 Left leaves P2 alone", w.pressed[1], 0);
	}
	{
		// Every standard P1 line reaches its own bit, nothing else.
		for (int i = 0; i < 12; i++)
		{
			Lines l;
			l.p1[i] = 1;
			arcade_panel::Words w = arcade_panel::Compute(l.p1, l.p2, l.twin);
			char what[64];
			std::snprintf(what, sizeof what, "P1 standard line %d alone", i);
			expect(what, w.pressed[0], arcade_panel::kPlayerBits[i]);
		}
	}
	{
		Lines l;
		l.p1[B1] = 1;
		l.p1[B3] = 1;
		arcade_panel::Words w = arcade_panel::Compute(l.p1, l.p2, l.twin);
		expect("P1 Buttons 1 and 3", w.pressed[0], 0x02 | 0x8000);
	}
	{
		// A twin-stick line that shares a bit presses it too.
		Lines l;
		l.twin[RLEVER_DOWN] = 1;
		arcade_panel::Words w = arcade_panel::Compute(l.p1, l.p2, l.twin);
		expect("right lever down is the left bit", w.pressed[0], 0x08);
	}
	{
		Lines l;
		l.p2[B2] = 1;
		arcade_panel::Words w = arcade_panel::Compute(l.p1, l.p2, l.twin);
		expect("P2 Button 2", w.pressed[1], 0x01);
		expect("P2 Button 2 leaves P1 alone", w.pressed[0], 0);
	}
	{
		// the negative control: the old order loses P1 Left
		Lines l;
		l.p1[LEFT] = 1;
		if (OldPlayerOne(l) == 0x08)
		{
			std::printf("FAIL negative control: the old order kept P1 Left, so this test cannot see the bug\n");
			failures++;
		}
	}

	if (failures == 0)
		std::printf("test-arcade-panel: ok\n");
	return failures == 0 ? 0 : 1;
}
