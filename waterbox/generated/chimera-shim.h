/* What both flavors of this core are compiled with, so that they agree.
 *
 * The guest (musl) and the native reference (glibc) do not offer the same C
 * library, and the differences are exactly the kind that would make an
 * equivalence gate compare two different machines. This header is force
 * included into every translation unit of both builds, so the answer to each
 * one is the same on both sides.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* The kernel's thread id, which glibc has and musl does not. There is one
 * thread, it never changes, and PCSX2 only uses the number to label threads. */
int chimera_gettid(void);

#ifdef __cplusplus
}
#endif
