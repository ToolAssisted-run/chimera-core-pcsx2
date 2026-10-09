/* What the hardware renderer drew, kept across a state load (chimera issue 190).
 *
 * THE LOSS. A whole-machine savestate is this core's memory, and under the
 * OpenGL renderer the picture is not in it: a render target is a texture on
 * the card, and the GS's own video memory under it is whatever was last
 * written THERE, which for a frame buffer is nothing at all. A load then gives
 * the renderer a new GL context (every load does, on purpose), the rebuild
 * threw the texture cache away and let targets be made again from that video
 * memory - and on a GTX 1060 the first frame after a load was an empty one,
 * and the three after it were off in one pixel in fifteen: the frame the game
 * had drawn and not yet shown, the depth it had not yet cleared, and the two
 * fields the deinterlacer weaves each picture from were gone.
 *
 * THE ANSWER. The engine says when it is about to take a state (the
 * StateSaving export). Every render target the texture cache holds, colour and
 * depth, and the deinterlacer's textures in the device, are copied then into a
 * block of this core's own memory, which a state carries; and after a load the
 * rebuild keeps the texture cache's targets, gives each a new texture, and
 * puts its pixels back, the device's textures likewise. Read and written in
 * the format the texture is held in, so what comes back is what was there.
 *
 * NOT INTO THE GS'S VIDEO MEMORY, which is where upstream's own "read targets
 * when closing" puts them. A game can read that memory, and the renderer
 * itself falls back on it when a target goes; a run that took a state would
 * then hold other bytes there than a run that did not, and a movie must not
 * depend on when somebody saved.
 *
 * WHAT A STATE PAYS. A target whose pixels did not change since the last copy
 * is compared and not written again, so it costs a state nothing; the ones the
 * game is drawing into change every frame and are in every delta. At the
 * machine's own resolution that is a few megabytes; it grows with the square
 * of the internalResolution setting.
 *
 * WHAT IS NOT CARRIED. A texture the block has no room for or whose format
 * this file does not know comes back the old way, made again from video
 * memory. The textures of an effect in flight between two draws (the colour
 * clip target, a temporary depth) are not targets of the cache and are not
 * looked at: a state is taken at a frame's end, after the GS has flushed.
 *
 * AND WHAT CANNOT BE, ABOVE THE MACHINE'S OWN RESOLUTION. Drawn larger than
 * the PS2 drew, a sprite's edge samples a texel past what its texture was
 * loaded with - upstream's well-known upscaling lines - and what is there is
 * whatever the recycled texture held before: the history of the device's
 * texture pool, hundreds of textures deep. A new context's textures held
 * nothing. On a GTX 1060 at 3x that is 0.03% of the picture, on its edge rows,
 * for as long as the scene lasts (it was 0.34%, after four frames far worse);
 * at 1x nothing samples past an edge and every frame is exact. Carrying the
 * device's pool of spare targets as well was tried: it moved those pixels and
 * did not remove them.
 *
 * This file reaches into the texture cache, the device and the renderer as a
 * friend (patch 0026 names it in each); nothing else does.
 */
#ifdef CHIMERA_GUEST_GL

#include "GS/GS.h"
#include "GS/Renderers/Common/GSDevice.h"
#include "GS/Renderers/Common/GSRenderer.h"
#include "GS/Renderers/Common/GSTexture.h"
#include "GS/Renderers/HW/GSRendererHW.h"
#include "GS/Renderers/HW/GSTextureCache.h"
#include "GS/Renderers/OpenGL/GSTextureOGL.h"
#include "glad/gl.h"

#include "common/Console.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <vector>

#include <emulibc.h>

/* the "verbose" setting (cinterface.cpp owns it) */
extern "C" int chimera_gs_trace;

namespace
{
	constexpr u64 SHADOW_MAGIC = 0x3253504753584843ull; // "CHXSGPS2"
	constexpr u32 SHADOW_MAX_RECORDS = 1024;
	/* Address space, not memory: pages are committed as they are written.
	 * Room for what a game holds at four times the machine's resolution. */
	constexpr size_t SHADOW_BLOCK_BYTES = static_cast<size_t>(768) << 20;
	/* One band of a texture at a time is read here before it is compared. */
	constexpr size_t SHADOW_SCRATCH_BYTES = static_cast<size_t>(4) << 20;

	/* Who a record's texture belongs to: a target of the texture cache is
	 * named by its address, which a load brings back with the object; the
	 * device's own are named by what they are for. No address is this small. */
	enum : u64
	{
		OWNER_MERGE = 1,
		OWNER_WEAVEBOB = 2,
		OWNER_BLEND = 3,
		OWNER_MAD = 4,
		OWNER_LAST_ROLE = 4,
	};

	struct ShadowHeader
	{
		u64 magic;
		u32 count;
		u32 current; // the OWNER_ of the device's finished frame, 0 if none of them
		u64 end; // bytes of pixels in use
		u64 unused;
	};

	struct ShadowRecord
	{
		u64 owner;
		u64 offset; // into the pixels
		u64 bytes; // 0: nothing was read - the texture holds a clear that had not been drawn yet, or nothing
		s32 width;
		s32 height;
		u32 gl_format;
		u32 usage; // GSTexture::Usage
		u32 format; // GSTexture::Format
		u32 state; // GSTexture::State
		u32 clear; // the pending clear value's bits, colour or depth
		u32 unused;
	};

	constexpr size_t SHADOW_TABLE_BYTES = sizeof(ShadowHeader) + SHADOW_MAX_RECORDS * sizeof(ShadowRecord);

	/* The block is ordinary memory: a state carries it. The pointers to it and
	 * to the scratch are not, so that a state made before Init finished cannot
	 * put a null back. */
	ECL_INVISIBLE u8* s_block;
	ECL_INVISIBLE u8* s_scratch;
	ECL_INVISIBLE ShadowRecord* s_fresh;

	/* The block describes the textures as they are now: set by a copy, cleared
	 * when a frame begins. In a state, so a state taken with the core left
	 * untold says so itself. */
	bool s_current;
	/* What the last load brought, noted after it (StateLoaded) and used by the
	 * rebuild that follows. */
	bool s_usable;

	ShadowHeader* Header() { return reinterpret_cast<ShadowHeader*>(s_block); }
	ShadowRecord* Records() { return reinterpret_cast<ShadowRecord*>(s_block + sizeof(ShadowHeader)); }
	u8* Pixels() { return s_block + SHADOW_TABLE_BYTES; }
	constexpr size_t SHADOW_PIXEL_BYTES = SHADOW_BLOCK_BYTES - SHADOW_TABLE_BYTES;

	/* The transfer that loses nothing, for each format a target is held in. */
	bool Lossless(u32 gl_format, GLenum* format, GLenum* type, u32* texel_bytes)
	{
		switch (gl_format)
		{
			case GL_RGBA8:
				*format = GL_RGBA; *type = GL_UNSIGNED_BYTE; *texel_bytes = 4;
				return true;
			case GL_RGBA16:
				*format = GL_RGBA; *type = GL_UNSIGNED_SHORT; *texel_bytes = 8;
				return true;
			case GL_R32F:
				*format = GL_RED; *type = GL_FLOAT; *texel_bytes = 4;
				return true;
			case GL_R8:
				*format = GL_RED; *type = GL_UNSIGNED_BYTE; *texel_bytes = 1;
				return true;
			case GL_DEPTH32F_STENCIL8:
				*format = GL_DEPTH_STENCIL; *type = GL_FLOAT_32_UNSIGNED_INT_24_8_REV; *texel_bytes = 8;
				return true;
			case GL_DEPTH_COMPONENT32F:
				*format = GL_DEPTH_COMPONENT; *type = GL_FLOAT; *texel_bytes = 4;
				return true;
			default:
				return false;
		}
	}

	void DrainErrors()
	{
		while (glGetError() != GL_NO_ERROR)
		{
		}
	}

	/* A texture's pixels against what the block holds at `at`, band by band;
	 * written only where they differ. False if the card refused the read. */
	bool CopyOut(GSTextureOGL* tex, GLenum format, GLenum type, u32 texel_bytes, u8* at)
	{
		const int width = tex->GetWidth();
		const int height = tex->GetHeight();
		const size_t row_bytes = static_cast<size_t>(width) * texel_bytes;
		const int band = std::max(1, static_cast<int>(SHADOW_SCRATCH_BYTES / row_bytes));
		for (int y = 0; y < height; y += band)
		{
			const int rows = std::min(band, height - y);
			const size_t bytes = row_bytes * rows;
			glGetTextureSubImage(tex->GetID(), 0, 0, y, 0, width, rows, 1, format, type,
				static_cast<GLsizei>(SHADOW_SCRATCH_BYTES), s_scratch);
			if (glGetError() != GL_NO_ERROR)
			{
				DrainErrors();
				return false;
			}
			/* the 24 bits beside a stencil value are nobody's, and a driver
			 * may leave anything in them */
			if (type == GL_FLOAT_32_UNSIGNED_INT_24_8_REV)
			{
				u32* words = reinterpret_cast<u32*>(s_scratch);
				const size_t texels = bytes / 8;
				for (size_t i = 0; i < texels; i++)
					words[i * 2 + 1] &= 0xFFu;
			}
			u8* dst = at + row_bytes * y;
			if (std::memcmp(dst, s_scratch, bytes) != 0)
				std::memcpy(dst, s_scratch, bytes);
		}
		return true;
	}
} // namespace

struct ChimeraGSShadow
{
	static GSTexture** Role(GSDevice* dev, u64 owner)
	{
		switch (owner)
		{
			case OWNER_MERGE: return &dev->m_merge;
			case OWNER_WEAVEBOB: return &dev->m_weavebob;
			case OWNER_BLEND: return &dev->m_blend;
			case OWNER_MAD: return &dev->m_mad;
			default: return nullptr;
		}
	}

	static void Save()
	{
		if (!s_block || !s_scratch || !s_fresh || !g_gs_device || !g_texture_cache)
			return;
		GSDevice* const dev = g_gs_device.get();

		GLint pack_buffer = 0, pack_alignment = 4, pack_row_length = 0;
		glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack_buffer);
		glGetIntegerv(GL_PACK_ALIGNMENT, &pack_alignment);
		glGetIntegerv(GL_PACK_ROW_LENGTH, &pack_row_length);
		if (pack_buffer != 0)
			glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glPixelStorei(GL_PACK_ROW_LENGTH, 0);
		DrainErrors();

		u32 count = 0;
		u64 end = 0;
		const auto add = [&](u64 owner, GSTexture* texture) {
			if (!texture || count == SHADOW_MAX_RECORDS)
				return;
			GSTextureOGL* const tex = static_cast<GSTextureOGL*>(texture);
			GLenum format = 0, type = 0;
			u32 texel_bytes = 0;
			if (tex->GetWidth() <= 0 || tex->GetHeight() <= 0 || !Lossless(tex->GetGLFormat(), &format, &type, &texel_bytes))
				return;

			ShadowRecord rec = {};
			rec.owner = owner;
			rec.offset = end;
			rec.width = tex->GetWidth();
			rec.height = tex->GetHeight();
			rec.gl_format = tex->GetGLFormat();
			rec.usage = static_cast<u32>(tex->GetUsage());
			rec.format = static_cast<u32>(tex->GetFormat());
			rec.state = static_cast<u32>(tex->GetState());
			/* A clear that has not been drawn yet is in the object and not on
			 * the card, and one that was thrown away has nothing worth
			 * keeping: neither is read, and each comes back as it was. */
			if (tex->GetState() == GSTexture::State::Cleared)
				rec.clear = tex->GetClearColor();
			if (tex->GetState() == GSTexture::State::Dirty)
			{
				const u64 bytes = static_cast<u64>(rec.width) * rec.height * texel_bytes;
				if (end + bytes > SHADOW_PIXEL_BYTES)
					return;
				if (!CopyOut(tex, format, type, texel_bytes, Pixels() + end))
					return;
				rec.bytes = bytes;
				end += bytes;
			}
			s_fresh[count++] = rec;
		};

		for (int type = 0; type < 2; type++)
		{
			for (GSTextureCache::Target* t : g_texture_cache->m_dst[type])
				add(reinterpret_cast<u64>(t), t->m_texture);
		}

		/* The device's: the two the deinterlacer keeps a field in from one
		 * picture to the next, always; the two that are only ever a result,
		 * when that result is the frame the frontend will read. */
		u32 current = 0;
		for (u64 owner = OWNER_MERGE; owner <= OWNER_LAST_ROLE; owner++)
		{
			GSTexture* const tex = *Role(dev, owner);
			if (tex && tex == dev->m_current)
				current = static_cast<u32>(owner);
		}
		add(OWNER_WEAVEBOB, dev->m_weavebob);
		add(OWNER_MAD, dev->m_mad);
		if (current == OWNER_MERGE)
			add(OWNER_MERGE, dev->m_merge);
		if (current == OWNER_BLEND)
			add(OWNER_BLEND, dev->m_blend);

		glPixelStorei(GL_PACK_ALIGNMENT, pack_alignment);
		glPixelStorei(GL_PACK_ROW_LENGTH, pack_row_length);
		if (pack_buffer != 0)
			glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(pack_buffer));

		ShadowHeader fresh = {};
		fresh.magic = SHADOW_MAGIC;
		fresh.count = count;
		fresh.current = current;
		fresh.end = end;
		if (std::memcmp(Header(), &fresh, sizeof(fresh)) != 0)
			*Header() = fresh;
		if (count != 0 && std::memcmp(Records(), s_fresh, count * sizeof(ShadowRecord)) != 0)
			std::memcpy(Records(), s_fresh, count * sizeof(ShadowRecord));
		if (!s_current)
			s_current = true;

		if (chimera_gs_trace)
		{
			u32 from_target = 0, copies = 0;
			for (const GSTextureCache::Source* src : g_texture_cache->m_src.m_surfaces)
			{
				from_target += src->m_target ? 1 : 0;
				copies += (src->m_target && !src->m_shared_texture) ? 1 : 0;
			}
			std::fprintf(stderr, "chimera gs: %u textures copied for a state, %llu bytes; %zu sources, %u from a target, %u of them copies\n",
				count, static_cast<unsigned long long>(end), g_texture_cache->m_src.m_surfaces.size(), from_target, copies);
			for (u32 i = 0; i < count; i++)
				std::fprintf(stderr, "chimera gs:   owner %llx %dx%d format %x state %u, %llu bytes\n",
					static_cast<unsigned long long>(s_fresh[i].owner), s_fresh[i].width, s_fresh[i].height,
					s_fresh[i].gl_format, s_fresh[i].state, static_cast<unsigned long long>(s_fresh[i].bytes));
		}
	}

	static const ShadowRecord* Find(u64 owner)
	{
		const ShadowHeader* const h = Header();
		const ShadowRecord* const recs = Records();
		for (u32 i = 0; i < h->count; i++)
		{
			if (recs[i].owner == owner)
				return &recs[i];
		}
		return nullptr;
	}

	/* A texture of the new device for a record, holding what the record holds. */
	static GSTexture* Remake(const ShadowRecord& rec)
	{
		GLenum format = 0, type = 0;
		u32 texel_bytes = 0;
		if (!Lossless(rec.gl_format, &format, &type, &texel_bytes))
			return nullptr;
		if (rec.offset + rec.bytes > SHADOW_PIXEL_BYTES)
			return nullptr;
		GSTexture* const made = g_gs_device->FetchSurface(static_cast<GSTexture::Usage>(rec.usage), rec.width, rec.height, 1,
			static_cast<GSTexture::Format>(rec.format), false, false);
		if (!made)
			return nullptr;
		GSTextureOGL* const tex = static_cast<GSTextureOGL*>(made);
		if (tex->GetWidth() != rec.width || tex->GetHeight() != rec.height || tex->GetGLFormat() != rec.gl_format)
		{
			delete made;
			return nullptr;
		}

		const GSTexture::State state = static_cast<GSTexture::State>(rec.state);
		if (state == GSTexture::State::Dirty && rec.bytes == static_cast<u64>(rec.width) * rec.height * texel_bytes)
		{
			glTextureSubImage2D(tex->GetID(), 0, 0, 0, rec.width, rec.height, format, type, Pixels() + rec.offset);
			if (glGetError() != GL_NO_ERROR)
			{
				DrainErrors();
				delete made;
				return nullptr;
			}
			made->SetState(GSTexture::State::Dirty);
		}
		else if (state == GSTexture::State::Cleared)
		{
			/* the same bits, colour or depth: the two share their storage */
			made->SetClearColor(rec.clear);
			made->SetState(GSTexture::State::Cleared);
		}
		else
		{
			made->SetState(GSTexture::State::Invalidated);
		}
		return made;
	}

	/* The renderer's GL objects again, for the context the calls land on now.
	 * With `keep`, the texture cache's targets and the deinterlacer's textures
	 * come back holding what a state carried; without, everything is made
	 * again from the GS's memory, as it always was. */
	static bool Rebuild(bool keep)
	{
		const bool usable = keep && s_usable && s_block && Header()->magic == SHADOW_MAGIC &&
		                    Header()->count <= SHADOW_MAX_RECORDS && g_texture_cache && g_gs_renderer;
		s_usable = false;
		if (!usable)
			return GSreopen(true, false, GSConfig.Renderer, std::nullopt);

		if (chimera_gs_trace)
			std::fprintf(stderr, "chimera gs: the render targets are kept across the rebuild (%u textures, %llu bytes)\n",
				Header()->count, static_cast<unsigned long long>(Header()->end));

		/* EVERYTHING OF THE OLD CONTEXT IS LET GO BEFORE ANYTHING IS MADE IN THE
		 * NEW ONE. The names are the dead context's, and deleting one after the
		 * new context has handed the same number out again deletes the new
		 * object. No flush first: what the GS has queued is vertices in this
		 * core's memory, and it is drawn later, into the targets as they come
		 * back - drawn now, it would go to names nobody holds. */
		g_gs_renderer->PurgeTextureCache(true, false, true);
		for (int type = 0; type < 2; type++)
		{
			for (GSTextureCache::Target* t : g_texture_cache->m_dst[type])
			{
				if (!t->m_texture)
					continue;
				g_texture_cache->m_target_memory_usage -= t->m_texture->GetMemUsage();
				delete t->m_texture;
				t->m_texture = nullptr;
			}
		}
		g_gs_device->ClearCurrent();
		g_gs_device->PurgePool();
		DrainErrors();
		if (!ChimeraGSReopenDevice())
		{
			Console.Error("chimera: the GS device did not open again");
			return false;
		}
		DrainErrors();

		GLint unpack_buffer = 0, unpack_alignment = 4, unpack_row_length = 0;
		glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack_buffer);
		glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment);
		glGetIntegerv(GL_UNPACK_ROW_LENGTH, &unpack_row_length);
		if (unpack_buffer != 0)
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		DrainErrors();

		u32 kept = 0, dropped = 0;
		GSRendererHW* const hw = GSRendererHW::GetInstance();
		for (int type = 0; type < 2; type++)
		{
			auto& list = g_texture_cache->m_dst[type];
			for (auto it = list.begin(); it != list.end();)
			{
				GSTextureCache::Target* const t = *it;
				const ShadowRecord* const rec = Find(reinterpret_cast<u64>(t));
				GSTexture* const made = rec ? Remake(*rec) : nullptr;
				if (made)
				{
					t->m_texture = made;
					g_texture_cache->m_target_memory_usage += made->GetMemUsage();
					kept++;
					++it;
					continue;
				}
				/* nothing to give it back: it goes, as every target used to,
				 * and the next draw that wants it makes it from video memory */
				if (hw && hw->m_last_rt == t)
					hw->m_last_rt = nullptr;
				it = list.erase(it);
				delete t;
				dropped++;
			}
		}

		GSDevice* const dev = g_gs_device.get();
		for (u64 owner = OWNER_MERGE; owner <= OWNER_LAST_ROLE; owner++)
		{
			const ShadowRecord* const rec = Find(owner);
			if (!rec)
				continue;
			GSTexture* const made = Remake(*rec);
			if (!made)
				continue;
			*Role(dev, owner) = made;
			if (Header()->current == owner)
				dev->m_current = made;
			kept++;
		}

		glPixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, unpack_row_length);
		if (unpack_buffer != 0)
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(unpack_buffer));
		DrainErrors();

		if (chimera_gs_trace || dropped != 0)
			std::fprintf(stderr, "chimera gs: %u textures came back, %u targets had nothing kept for them\n", kept, dropped);
		return true;
	}
};

/* Taken DURING INIT and never later, for the reason gs-device.cpp gives for
 * the frame it reads back into: what a state leaves alone in the invisible
 * heap is the contents of its pages, not whether they are mapped. The block
 * itself is ordinary memory, mapped here so that every state there will ever
 * be has it at the same place. */
extern "C" void ChimeraGSShadowReserve(void)
{
	if (s_block)
		return;
	void* const block = mmap(nullptr, SHADOW_BLOCK_BYTES, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (block == MAP_FAILED)
	{
		std::fprintf(stderr, "chimera: no room for the render targets a state carries; a load will lose them\n");
		return;
	}
	s_scratch = static_cast<u8*>(alloc_invisible(SHADOW_SCRATCH_BYTES));
	s_fresh = static_cast<ShadowRecord*>(alloc_invisible(SHADOW_MAX_RECORDS * sizeof(ShadowRecord)));
	s_block = static_cast<u8*>(block);
}

/* A frame is about to run: what the block holds stops being the textures. */
extern "C" void ChimeraGSShadowFrameBegins(void)
{
	if (s_current)
		s_current = false;
}

/* After a load: is the block the loaded machine's textures? */
extern "C" void ChimeraGSShadowStateLoaded(void)
{
	s_usable = s_current;
}

/* Before a state is taken, with the GL objects known to be the live context's. */
extern "C" void ChimeraGSShadowSave(void)
{
	ChimeraGSShadow::Save();
}

extern "C" bool ChimeraGSShadowRebuild(bool keep)
{
	return ChimeraGSShadow::Rebuild(keep);
}

#endif /* CHIMERA_GUEST_GL */
