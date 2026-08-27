/* What PCSX2 asks its build system for. A Chimera core's version is the commit
 * its package was built from (stamped into build.json by build-package.sh), so
 * what goes here is a fixed string rather than anything that changes with the
 * moment - two builds of one commit must be the same bytes. */
#pragma once
#define GIT_TAG ""
#define GIT_TAGGED_COMMIT 0
#define GIT_HASH "chimera"
#define GIT_REV "chimera"
#define GIT_DATE "2026-08-27"
#define SVN_REV 0
#define SVN_MODS 0
