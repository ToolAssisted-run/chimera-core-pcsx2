Content this repository may legally ship, and therefore does track. The sibling
`tests/roms/` is ignored wholesale (see the .gitignore, which explains why): a
PlayStation 2 needs a bios dump and the discs are somebody's, so nothing there
can be committed and no per-file rule is trusted to keep it that way. Anything
free to distribute goes here instead, where tracking it needs no exception.

- `padtest.elf` - a PlayStation 2 controller tester by jbit, which draws every
  button and stick of a DualShock 2 as the machine sees them. Free to
  distribute; the source is
  https://www.psx-place.com/resources/ps2-controller-tester-by-jbit.670/
  It is what the input gate reads: a core can only claim its buttons arrive in
  the right places if something on the machine says so.
