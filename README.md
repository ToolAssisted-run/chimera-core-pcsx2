# chimera-core-pcsx2

PCSX2 - the PlayStation 2 - as a Chimera waterbox core.

Upstream is `PCSX2/pcsx2`, pinned as a submodule at `extern/pcsx2`.

Two things make this port different from every other core in the bundle. It
comes with a real SOFTWARE renderer, maintained upstream for accuracy, so the
picture is not the problem it was for Flycast. And it cannot run ANYTHING
without a bios - a PS2 has no HLE bios - so its gates skip, loudly, until one
is supplied locally (never committed; see .gitignore).

The feasibility work, the two structural problems and the schedule are in
[`docs/PLAN.md`](docs/PLAN.md).

Status: **M1, the machine building and initialising.** Nothing runs yet.
