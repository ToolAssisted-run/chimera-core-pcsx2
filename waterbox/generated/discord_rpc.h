/* Rich presence, refused rather than patched out.
 *
 * PCSX2 tells a chat service what someone is playing. A core that a movie must
 * replay has no business talking to one, and a sandbox could not reach it: the
 * calls stay where upstream put them and do nothing, which keeps the patch set
 * for changes that matter.
 */
#pragma once

typedef struct DiscordRichPresence
{
	const char *state;
	const char *details;
	long long startTimestamp;
	long long endTimestamp;
	const char *largeImageKey;
	const char *largeImageText;
	const char *smallImageKey;
	const char *smallImageText;
	const char *partyId;
	int partySize;
	int partyMax;
	const char *matchSecret;
	const char *joinSecret;
	const char *spectateSecret;
	signed char instance;
} DiscordRichPresence;

typedef struct DiscordEventHandlers
{
	void (*ready)(void);
	void (*disconnected)(int errorCode, const char *message);
	void (*errored)(int errorCode, const char *message);
	void (*joinGame)(const char *joinSecret);
	void (*spectateGame)(const char *spectateSecret);
	void (*joinRequest)(void *request);
} DiscordEventHandlers;

static inline void Discord_Initialize(const char *, DiscordEventHandlers *, int, const char *) {}
static inline void Discord_Shutdown(void) {}
static inline void Discord_RunCallbacks(void) {}
static inline void Discord_UpdatePresence(const DiscordRichPresence *) {}
static inline void Discord_ClearPresence(void) {}
static inline void Discord_Respond(const char *, int) {}
