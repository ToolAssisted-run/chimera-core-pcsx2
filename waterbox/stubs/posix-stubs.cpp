/* The few POSIX calls PCSX2 makes that a sandbox has no syscall for, and the
 * two glibc extensions musl does not carry.
 *
 * A waterbox guest talks to the host through a small, deliberate surface: the
 * files the frontend mounted, and nothing else. There is no kernel behind it,
 * so a call like access() does not fail - it stops the machine, because the
 * sandbox refuses to guess what the guest meant.
 *
 * These are answered in terms of what the sandbox DOES have. They are honest
 * answers, not silent successes: a file the frontend did not mount is reported
 * absent, which is exactly what it is.
 */
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

extern "C" int access(const char *path, int mode)
{
	/* Existence and readability are the same question here: the sandbox hands
	 * the guest read-only mounts, so a file that opens is readable and one
	 * that does not is absent. Writability is answered no - the machine's
	 * writes go through the save-data channel, never through a path. */
	if (mode & W_OK)
	{
		errno = EACCES;
		return -1;
	}
	FILE *f = fopen(path, "rb");
	if (f == nullptr)
	{
		errno = ENOENT;
		return -1;
	}
	fclose(f);
	return 0;
}

/* The kernel's thread id. There is one thread, it never changes, and nothing
 * in the machine depends on the number - PCSX2 uses it to name threads in a
 * profiler. glibc exposes it; musl does not.
 */
extern "C" int chimera_gettid(void) { return 1; }

/* Making a directory.
 *
 * PCSX2 keeps its own house on a desktop: a folder for memory cards, one for
 * savestates, one for snapshots, and it creates them if they are missing. The
 * sandbox has no such house - everything the machine is made of arrives
 * mounted at the root, and everything it produces leaves through the ABI - so
 * there is nothing to create and nowhere to create it.
 *
 * The root itself is reported as already there, which is true; anything else
 * is refused as read-only, which is also true. Both are answers PCSX2 already
 * handles, because both are what it would get from a read-only medium.
 */
extern "C" int mkdir(const char *path, mode_t mode)
{
	if (path && (!strcmp(path, ".") || !strcmp(path, "./") || !strcmp(path, "/")))
	{
		errno = EEXIST;
		return -1;
	}

	errno = EROFS;
	return -1;
}

/* Naming a thread.
 *
 * PCSX2 labels its threads so a profiler can tell them apart. There is one
 * thread here and no profiler, and the sandbox has no kernel to hold the name.
 */
extern "C" int prctl(int option, ...) { return 0; }

/* Where the machine is.
 *
 * PCSX2 resolves a relative path by asking where the process is standing. A
 * sandbox stands in the one directory it has, which is where every mounted
 * file already is - so the answer is "here", and a path resolved against it
 * comes back unchanged.
 */
extern "C" char *getcwd(char *buf, size_t size)
{
	if (!buf || size < 2)
	{
		errno = ERANGE;
		return nullptr;
	}

	buf[0] = '.';
	buf[1] = '\0';
	return buf;
}

/* Following a link.
 *
 * There are no links in a sandbox - there is a flat list of mounted files - so
 * asking about a name without following links is the same question as asking
 * about the file.
 */
extern "C" int lstat(const char *path, struct stat *st)
{
	return stat(path, st);
}
