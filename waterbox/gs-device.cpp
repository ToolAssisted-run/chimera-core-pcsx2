/* A graphics device made of memory.
 *
 * PCSX2's software renderer draws the PS2's picture with the CPU, into the
 * emulated console's own video memory - which is exactly what a sandboxed core
 * wants. What it does NOT want is the last step: PCSX2 then hands that picture
 * to a GSDevice, meaning Direct3D, Vulkan, OpenGL or Metal, so a window can
 * show it. There is no window here, and no GPU.
 *
 * So this is a GSDevice whose textures are `std::vector<u8>` and whose drawing
 * operations are memcpy. It implements the handful of things the software path
 * actually asks for:
 *
 *   CreateSurface   a texture is width * height * 4 bytes of RGBA8
 *   Update / Map    the renderer writing its frame into one
 *   DoMerge         the PS2 has TWO display circuits; this is where they are
 *                   combined into the picture a television would show
 *   DoInterlace     and where a field is placed into that picture
 *   DoStretchRect   a scaled copy, point-sampled
 *   CopyRect        a copy
 *
 * and answers everything else - shaders, CAS, FXAA, GPU timing, windows,
 * fullscreen - the way a machine with no GPU should: not supported, nothing
 * done. The hardware renderer's entry point (RenderHW) is unreachable, because
 * patch 0002 leaves no hardware renderer in the build.
 *
 * The result is that GSDevice::GetCurrent() holds the finished frame in plain
 * memory after every vsync, which is what cinterface.cpp hands the frontend.
 *
 * Point sampling rather than bilinear is deliberate: it is what the console
 * does when it scans out, it is reproducible to the bit, and a movie that
 * replays on another machine must not depend on how someone's filter rounded.
 */
#include "GS/GS.h"
#ifdef CHIMERA_GUEST_GL
#include "GS/Renderers/OpenGL/GSTextureOGL.h"
#include "glad/gl.h"
#include <vector>
#endif
#include "GS/Renderers/Common/GSDevice.h"
#include "GS/Renderers/Common/GSTexture.h"

#include "common/Console.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
	/* ---------------------------------------------------------------------
	 * A texture: a size, a format, and the bytes.
	 */
	class ChimeraTexture final : public GSTexture
	{
	public:
		ChimeraTexture(Usage usage, int width, int height, int levels, Format format)
		{
			m_size = GSVector2i(width, height);
			m_mipmap_levels = std::max(levels, 1);
			m_usage = usage;
			m_format = format;
			m_pitch = width * BytesPerPixel(format);
			m_data.resize(static_cast<size_t>(m_pitch) * static_cast<size_t>(std::max(height, 1)));
		}

		static int BytesPerPixel(Format format)
		{
			switch (format)
			{
				case Format::UNorm8: return 1;
				case Format::UInt16: return 2;
				case Format::ColorHDR:
				case Format::ColorClip: return 8;
				default: return 4;
			}
		}

		void* GetNativeHandle() const override { return const_cast<u8*>(m_data.data()); }

		bool Update(const GSVector4i& r, const void* data, int pitch, int layer) override
		{
			if (layer != 0)
				return true;

			const int bpp = BytesPerPixel(m_format);
			const int rows = std::min(r.height(), m_size.y - r.y);
			const int bytes = std::min(r.width(), m_size.x - r.x) * bpp;
			if (rows <= 0 || bytes <= 0)
				return true;

			const u8* src = static_cast<const u8*>(data);
			u8* dst = m_data.data() + static_cast<size_t>(r.y) * m_pitch + static_cast<size_t>(r.x) * bpp;
			for (int y = 0; y < rows; y++, src += pitch, dst += m_pitch)
				std::memcpy(dst, src, bytes);

			m_state = State::Dirty;
			return true;
		}

		bool Map(GSMap& m, const GSVector4i* r, int layer) override
		{
			if (layer != 0)
				return false;

			const int bpp = BytesPerPixel(m_format);
			const int x = r ? r->x : 0;
			const int y = r ? r->y : 0;
			m.bits = m_data.data() + static_cast<size_t>(y) * m_pitch + static_cast<size_t>(x) * bpp;
			m.pitch = m_pitch;
			m_state = State::Dirty;
			return true;
		}

		void Unmap() override {}
		void GenerateMipmap() override {}
#ifdef PCSX2_DEVBUILD
		void SetDebugName(std::string_view name) override {}
#endif

		int GetPitch() const { return m_pitch; }
		u8* GetBits() { return m_data.data(); }
		const u8* GetBits() const { return m_data.data(); }

	private:
		int m_pitch = 0;
		std::vector<u8> m_data;
	};

	/* Set CHIMERA_GS_TRACE to see how a frame is assembled: which display
	 * circuits are on, and where each lands in the picture. It is how the
	 * first game's doubled image was traced to PCSX2's adaptive deinterlacer.
	 */
	bool ChimeraGSTrace()
	{
		static const bool on = getenv("CHIMERA_GS_TRACE") != nullptr;
		return on;
	}

	/* A point-sampled scaled copy: the one drawing operation this device
	 * really performs. Everything else here is a special case of it.
	 */
	void BlitScaled(ChimeraTexture* src, const GSVector4& sRect, ChimeraTexture* dst, const GSVector4& dRect)
	{
		if (!src || !dst)
			return;

		const int bpp = ChimeraTexture::BytesPerPixel(dst->GetFormat());
		if (bpp != ChimeraTexture::BytesPerPixel(src->GetFormat()))
			return;

		/* The rects arrive normalised (0..1) from the shader-shaped API. */
		const GSVector2i ssize = src->GetSize();
		const GSVector2i dsize = dst->GetSize();
		const float su0 = sRect.x * static_cast<float>(ssize.x);
		const float sv0 = sRect.y * static_cast<float>(ssize.y);
		const float su1 = sRect.z * static_cast<float>(ssize.x);
		const float sv1 = sRect.w * static_cast<float>(ssize.y);

		const int dx0 = std::max(static_cast<int>(dRect.x), 0);
		const int dy0 = std::max(static_cast<int>(dRect.y), 0);
		const int dx1 = std::min(static_cast<int>(dRect.z), dsize.x);
		const int dy1 = std::min(static_cast<int>(dRect.w), dsize.y);
		if (dx1 <= dx0 || dy1 <= dy0)
			return;

		const float dw = dRect.z - dRect.x;
		const float dh = dRect.w - dRect.y;
		if (dw <= 0.0f || dh <= 0.0f)
			return;

		for (int dy = dy0; dy < dy1; dy++)
		{
			const float v = sv0 + (sv1 - sv0) * ((static_cast<float>(dy) + 0.5f - dRect.y) / dh);
			const int sy = std::clamp(static_cast<int>(v), 0, ssize.y - 1);
			const u8* srow = src->GetBits() + static_cast<size_t>(sy) * src->GetPitch();
			u8* drow = dst->GetBits() + static_cast<size_t>(dy) * dst->GetPitch();

			for (int dx = dx0; dx < dx1; dx++)
			{
				const float u = su0 + (su1 - su0) * ((static_cast<float>(dx) + 0.5f - dRect.x) / dw);
				const int sx = std::clamp(static_cast<int>(u), 0, ssize.x - 1);
				std::memcpy(drow + static_cast<size_t>(dx) * bpp, srow + static_cast<size_t>(sx) * bpp, bpp);
			}
		}
	}

	/* ---------------------------------------------------------------------
	 * The device.
	 */
	class ChimeraDevice final : public GSDevice
	{
	public:
		ChimeraDevice()
		{
			m_max_texture_size = 4096;
			m_features.texture_barrier = false;
			m_features.broken_point_sampler = false;
			m_features.primitive_id = false;
			m_features.prefer_new_textures = false;
			m_features.dxt_textures = false;
			m_features.bptc_textures = false;
			m_features.framebuffer_fetch = false;
			m_features.stencil_buffer = false;
			m_features.cas_sharpening = false;
			m_features.test_and_sample_depth = false;
		}

		RenderAPI GetRenderAPI() const override { return RenderAPI::None; }
		bool HasSurface() const override { return true; }
		void DestroySurface() override {}
		bool UpdateWindow() override { return true; }
		void ResizeWindow(u32 width, u32 height, float scale) override {}
		bool SupportsExclusiveFullscreen() const override { return false; }

		/* There is no window to present into: the frontend takes the finished
		 * picture out of GetCurrent() instead. Saying "occluded" here would
		 * make the GS skip work; saying OK and then drawing nothing is what a
		 * device with no screen honestly does.
		 */
		PresentResult BeginPresent(bool frame_skip) override { return PresentResult::FrameSkipped; }
		void EndPresent() override {}
		void SetVSyncMode(GSVSyncMode mode, bool allow_present_throttle) override {}

		std::string GetDriverInfo() const override { return "Chimera software device"; }
		bool SetGPUTimingEnabled(bool enabled) override { return false; }
		float GetAndResetAccumulatedGPUTime() override { return 0.0f; }
		bool SetGPUPipelineStatisticsEnabled(bool enabled) override { return false; }
		GPUPipelineStatistics GetAndResetAccumulatedGPUPipelineStatistics() override { return {}; }

		void PushDebugGroup(const char* fmt, ...) override {}
		void PopDebugGroup() override {}
		void InsertDebugMessage(DebugMessageCategory category, const char* fmt, ...) override {}

		std::unique_ptr<GSDownloadTexture> CreateDownloadTexture(u32 width, u32 height,
			GSTexture::Format format) override
		{
			return nullptr;
		}

		void CopyRect(GSTexture* sTex, GSTexture* dTex, const GSVector4i& r, u32 destX, u32 destY) override
		{
			ChimeraTexture* src = static_cast<ChimeraTexture*>(sTex);
			ChimeraTexture* dst = static_cast<ChimeraTexture*>(dTex);
			if (!src || !dst)
				return;

			const int bpp = ChimeraTexture::BytesPerPixel(dst->GetFormat());
			const int rows = std::min(r.height(), dst->GetSize().y - static_cast<int>(destY));
			const int cols = std::min(r.width(), dst->GetSize().x - static_cast<int>(destX));
			for (int y = 0; y < rows; y++)
			{
				std::memcpy(dst->GetBits() + static_cast<size_t>(destY + y) * dst->GetPitch() + static_cast<size_t>(destX) * bpp,
					src->GetBits() + static_cast<size_t>(r.y + y) * src->GetPitch() + static_cast<size_t>(r.x) * bpp,
					static_cast<size_t>(cols) * bpp);
			}
		}

		void PresentRect(GSTexture* sTex, const GSVector4& sRect, GSTexture* dTex, const GSVector4& dRect,
			PresentShader shader, float shaderTime, Filter filter) override
		{
			BlitScaled(static_cast<ChimeraTexture*>(sTex), sRect, static_cast<ChimeraTexture*>(dTex), dRect);
		}

		void UpdateCLUTTexture(GSTexture* sTex, float sScale, u32 offsetX, u32 offsetY, GSTexture* dTex,
			u32 dOffset, u32 dSize) override {}
		void ConvertToIndexedTexture(GSTexture* sTex, float sScale, u32 offsetX, u32 offsetY, u32 SBW, u32 SPSM,
			GSTexture* dTex, u32 DBW, u32 DPSM) override {}
		void FilteredDownsampleTexture(GSTexture* sTex, GSTexture* dTex, u32 downsample_factor,
			const GSVector2i& clamp_min, const GSVector4& dRect) override {}

		/* Unreachable: patch 0002 leaves no hardware renderer to call it. */
		void RenderHW(GSHWDrawConfig& config) override {}
		void ClearSamplerCache() override {}

	protected:
		GSTexture* CreateSurface(GSTexture::Usage usage, int width, int height, int levels,
			GSTexture::Format format) override
		{
			return new ChimeraTexture(usage, width, height, levels, format);
		}

		/* The two display circuits, combined. PMODE says which are enabled and
		 * how they blend; the software path gives us each already in RGBA8, so
		 * a copy of the enabled ones in order is the picture.
		 */
		void DoMerge(GSTexture* sTex[3], GSVector4* sRect, GSTexture* dTex, GSVector4* dRect,
			const GSRegPMODE& PMODE, const GSRegEXTBUF& EXTBUF, u32 c, const Filter filter) override
		{
			ChimeraTexture* dst = static_cast<ChimeraTexture*>(dTex);
			if (!dst)
				return;

			if (ChimeraGSTrace())
			{
				fprintf(stderr, "merge: dst %dx%d EN1=%d EN2=%d\n", dst->GetSize().x, dst->GetSize().y, PMODE.EN1, PMODE.EN2);
				for (int i = 0; i < 2; i++)
				{
					if (!sTex[i]) { fprintf(stderr, "  [%d] none\n", i); continue; }
					fprintf(stderr, "  [%d] src %dx%d sRect %.3f %.3f %.3f %.3f dRect %.1f %.1f %.1f %.1f\n", i,
						sTex[i]->GetSize().x, sTex[i]->GetSize().y,
						sRect[i].x, sRect[i].y, sRect[i].z, sRect[i].w,
						dRect[i].x, dRect[i].y, dRect[i].z, dRect[i].w);
				}
			}

			/* The background the circuits are drawn over. */
			std::memset(dst->GetBits(), 0, static_cast<size_t>(dst->GetPitch()) * dst->GetSize().y);

			if (sTex[1] && PMODE.EN2)
				BlitScaled(static_cast<ChimeraTexture*>(sTex[1]), sRect[1], dst, dRect[1]);
			if (sTex[0] && PMODE.EN1)
				BlitScaled(static_cast<ChimeraTexture*>(sTex[0]), sRect[0], dst, dRect[0]);
		}

		/* A field placed into the frame. The shader variants differ in how
		 * they blend fields together; this device takes the field as it is,
		 * which is the "no deinterlacing" answer and the reproducible one.
		 */
		void DoInterlace(GSTexture* sTex, const GSVector4& sRect, GSTexture* dTex, const GSVector4& dRect,
			ShaderInterlace shader, Filter filter, const InterlaceConstantBuffer& cb) override
		{
			if (ChimeraGSTrace())
				fprintf(stderr, "interlace: shader=%d src %dx%d sRect %.3f %.3f %.3f %.3f dRect %.1f %.1f %.1f %.1f\n",
					(int)shader, sTex ? sTex->GetSize().x : 0, sTex ? sTex->GetSize().y : 0,
					sRect.x, sRect.y, sRect.z, sRect.w, dRect.x, dRect.y, dRect.z, dRect.w);
			BlitScaled(static_cast<ChimeraTexture*>(sTex), sRect, static_cast<ChimeraTexture*>(dTex), dRect);
		}

		void DoFXAA(GSTexture* sTex, GSTexture* dTex) override {}
		void DoShadeBoost(GSTexture* sTex, GSTexture* dTex, const float params[4]) override {}
		bool DoCAS(GSTexture* sTex, GSTexture* dTex, bool sharpen_only,
			const std::array<u32, NUM_CAS_CONSTANTS>& constants) override
		{
			return false;
		}

		void DoStretchRect(GSTexture* sTex, const GSVector4& sRect, GSTexture* dTex, const GSVector4& dRect,
			ShaderConvertSelector shader, Filter filter) override
		{
			BlitScaled(static_cast<ChimeraTexture*>(sTex), sRect, static_cast<ChimeraTexture*>(dTex), dRect);
		}
	};
} // namespace

std::unique_ptr<GSDevice> MakeChimeraGSDevice()
{
	return std::make_unique<ChimeraDevice>();
}

/* The finished frame, for cinterface.cpp: the bytes of whatever the GS last
 * presented, or nothing if it has not presented yet.
 */
extern "C" bool ChimeraGSGetFrame(const u8** bits, int* pitch, int* width, int* height)
{
	GSTexture* current = g_gs_device ? g_gs_device->GetCurrent() : nullptr;
	if (!current)
		return false;

#ifdef CHIMERA_GUEST_GL
	/* Under the OpenGL renderer the frame is a GL texture rather than a piece
	 * of this core's memory, so it is read back into one. glGetTextureImage
	 * rather than a framebuffer read: what was presented is this texture, and
	 * whatever is in framebuffer 0 is whatever was left lying there. */
	if (g_gs_device->GetRenderAPI() == RenderAPI::OpenGL)
	{
		static std::vector<u8> readback;
		const GSVector2i size = current->GetSize();
		const size_t need = (size_t)size.x * size.y * 4;
		if (readback.size() < need)
			readback.resize(need);

		glGetTextureImage(static_cast<GSTextureOGL*>(current)->GetID(), 0,
			GL_RGBA, GL_UNSIGNED_BYTE, (GLsizei)readback.size(), readback.data());

		*bits = readback.data();
		*pitch = size.x * 4;
		*width = size.x;
		*height = size.y;
		return true;
	}
#endif

	ChimeraTexture* tex = static_cast<ChimeraTexture*>(current);
	*bits = tex->GetBits();
	*pitch = tex->GetPitch();
	*width = tex->GetSize().x;
	*height = tex->GetSize().y;
	return true;
}
