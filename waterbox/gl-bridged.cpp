/* The guest's end of the GPU bridge: the context PCSX2's GL renderer thinks it
 * is drawing into, when the drawing is actually happening on a real GPU outside
 * the sandbox.
 *
 * There is no driver on this side. Every entry point the renderer calls is a
 * wrapper that packs its arguments and hands them to the host (see
 * waterbox/gl-bridge.h for why that is the only shape available). What this
 * file adds is the object PCSX2 asks for a context: something that answers
 * GetProcAddress, reports a version, and has nothing to swap into.
 *
 * The renderer itself is unmodified. It loads its entry points through
 * gladLoadGL over this object's GetProcAddress, exactly as it does on a
 * desktop - which is deliberate: glad then asks the DRIVER what version and
 * extensions it has, through the bridge, so what PCSX2 believes about the GPU
 * is what the GPU actually said.
 *
 * WHAT THIS COSTS: the GPU is outside the sandbox. It is outside the savestate,
 * outside this core's determinism, and different on every machine. A run
 * recorded this way is not guaranteed to replay anywhere, including here.
 */
#include "GS/Renderers/OpenGL/GLContext.h"
#include "common/Error.h"

#include "gl-bridge.h"   /* miniBox source/gl: the shared contract */

#include <glad/gl.h>

#include <algorithm>
#include <memory>

/* generated from miniBox's master list; install refuses a host whose list is
 * shorter than this core was built against */

namespace
{

chimera_gl_bridge_fn g_bridge;

class ChimeraBridgedGLContext final : public GLContext
{
public:
	explicit ChimeraBridgedGLContext(const WindowInfo& wi) : GLContext(wi) {}

	bool Create()
	{
		/* The version PCSX2 is told before glad runs. glad overwrites this from
		 * the driver's own GL_VERSION a moment later; this only has to be high
		 * enough that nothing refuses to start in between. */
		m_version.major_version = 3;
		m_version.minor_version = 3;
		return true;
	}

	/* Every name resolves to a wrapper, and glad fills its table with them. */
	void* GetProcAddress(const char* name) override
	{
		return chimera_gl_lookup(name);
	}

	bool ChangeSurface(const WindowInfo& new_wi) override
	{
		m_wi = new_wi;
		return true;
	}

	void ResizeSurface(u32 new_surface_width, u32 new_surface_height) override
	{
		m_wi.surface_width = new_surface_width;
		m_wi.surface_height = new_surface_height;
	}

	/* Nothing to swap into: the frontend takes the finished frame through the
	 * ABI, as it does from every other renderer this core has. */
	bool SwapBuffers() override { return true; }

	/* The host made the context current on its own thread before handing the
	 * guest the callback, and there is only one of each here. */
	bool IsCurrent() override { return true; }
	bool MakeCurrent() override { return true; }
	bool DoneCurrent() override { return true; }

	bool SupportsNegativeSwapInterval() const override { return false; }
	bool SetSwapInterval(s32 interval) override { return true; }

	std::unique_ptr<GLContext> CreateSharedContext(const WindowInfo& wi, Error* error) override
	{
		Error::SetStringView(error, "a sandboxed core has one thread and one context");
		return nullptr;
	}
};

} // namespace

/* Whether a host offered a GPU. Read by the stream buffer, which must not take
 * the persistent-mapping path when it did (see the patch that calls this). */
extern "C" bool chimera_gl_bridged()
{
	return g_bridge != nullptr;
}

/* Called before Init when the host has a context to offer. Installing the
 * wrappers here means the renderer finds them the moment it asks. */
extern "C" bool chimera_gl_bridge_start(chimera_gl_bridge_fn bridge)
{
	if (!bridge)
		return false;

	if (!chimera_gl_install(bridge))
		return false;   /* an older host; the softpipe draws instead */

	g_bridge = bridge;
	return true;
}

std::unique_ptr<GLContext> ChimeraCreateBridgedGLContext(const WindowInfo& wi, Error* error)
{
	if (!g_bridge)
	{
		Error::SetStringView(error, "no GPU bridge was offered");
		return nullptr;
	}

	std::unique_ptr<ChimeraBridgedGLContext> context =
		std::make_unique<ChimeraBridgedGLContext>(wi);
	if (!context->Create())
	{
		Error::SetStringView(error, "the GPU bridge would not start");
		return nullptr;
	}

	return context;
}
