#!/usr/bin/env python3
"""Generates both sides of the GPU bridge from glad's declarations.

A core reaches OpenGL the way most programs do: through glad, which declares
every entry point as a function POINTER and fills them in at startup. That is
the seam this bridge uses. Nothing in Flycast's renderer is modified - its
pointers are simply made to point at wrappers that hand the call to the host.

For each entry point this writes three things:

  gl-bridge-ops.h        an opcode, and a struct holding that call's arguments,
                         included by both sides so the layout is one definition
  gl-bridge-guest.cpp    the wrapper the guest calls: fill the struct, hand its
                         address to the sandbox's callback
  gl-bridge-host.inc     the host's answer: cast the struct back and make the
                         real call, on the real device

The set of entry points is not "all of OpenGL" - it is what Flycast's GLES
renderer actually names, which is about ninety functions. A name that turns up
later fails loudly at install time rather than silently doing nothing.

Usage: gen-gl-bridge.py <glad gl.h> <names file> <output dir>
"""
import re
import sys

# Two calls return a pointer into the DRIVER's memory, which a guest cannot
# read: the sandbox stops it, and should. They are answered by copying the
# string into a buffer on the guest's side and returning that instead.
STRING_RETURNING = {"glGetString", "glGetStringi"}

# Calls whose host side must do NOTHING, because honouring them would hand the
# driver something only the guest may touch. glDebugMessageCallback would give
# it a guest function pointer to call on a host thread, with neither the
# sandbox's ABI shim nor its stack: the wrapper is generated so the renderer
# still links and still "installs" its logger, and the host quietly declines.
HOST_IGNORES = {"glDebugMessageCallback"}


def parse_typedefs(header_text):
    """name -> (return type, [(type, name), ...]) from glad's PFN typedefs."""
    pattern = re.compile(
        r"typedef\s+(.+?)\s*\(GLAD_API_PTR\s*\*\s*PFN(\w+)PROC\)\s*\((.*?)\);",
        re.S)
    out = {}
    for ret, upper, params in pattern.findall(header_text):
        name = "gl" + upper[2:].lower() if False else None  # (see below)
        # glad spells the typedef in upper case: PFNGLDRAWARRAYSPROC. The
        # function's real spelling comes from the declaration list instead.
        out.setdefault(upper.upper(), (ret.strip(), params.strip()))
    return out


def parse_declarations(header_text):
    """The real spelling of each entry point, keyed by its typedef name."""
    pattern = re.compile(r"GLAD_API_CALL PFN(\w+)PROC glad_(\w+);")
    return {upper.upper(): real for upper, real in pattern.findall(header_text)}


def split_params(params):
    """['GLenum mode', 'GLint first'] -> [('GLenum', 'mode'), ...]"""
    if params.strip() in ("", "void"):
        return []
    parts, depth, current = [], 0, ""
    for ch in params:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(current)
            current = ""
        else:
            current += ch
    parts.append(current)

    result = []
    for i, part in enumerate(parts):
        part = part.strip()
        # the name is the last identifier; everything before it is the type
        match = re.match(r"^(.*?)(\w+)$", part.replace("[]", " *"))
        if not match or not match.group(1).strip():
            result.append((part, f"arg{i}"))
        else:
            result.append((match.group(1).strip(), match.group(2)))
    return result


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    header = open(sys.argv[1]).read()
    wanted = [line.strip() for line in open(sys.argv[2]) if line.strip()]
    outdir = sys.argv[3]

    typedefs = parse_typedefs(header)
    spellings = parse_declarations(header)
    by_name = {}
    for upper, real in spellings.items():
        if upper in typedefs:
            by_name[real] = typedefs[upper]

    missing = [n for n in wanted if n not in by_name]
    entries = [(n, *by_name[n]) for n in wanted if n in by_name]

    ops, guest, host = [], [], []
    for index, (name, ret, params) in enumerate(entries, start=100):
        args = split_params(params)
        struct = f"ChimeraGlArgs_{name}"

        ops.append(f"/* {ret} {name}({params}) */")
        ops.append(f"#define CHIMERA_GL_OP_{name} {index}")
        if args:
            ops.append(f"struct {struct} {{")
            for type_, arg in args:
                ops.append(f"\t{type_} {arg};")
            ops.append("};")
        ops.append("")

        signature = ", ".join(f"{t} {a}" for t, a in args) or "void"
        call_args = ", ".join(f"chimera_p->{a}" for t, a in args)

        if name in HOST_IGNORES:
            body_fill = "".join(f"\tchimera_a.{arg} = {arg};\n" for _, arg in args)
            guest.append(f"""static void GLAD_API_PTR w_{name}({signature})
{{
	struct {struct} chimera_a;
{body_fill}	g_bridge(CHIMERA_GL_OP_{name}, (uint64_t)(uintptr_t)&chimera_a, 0, 0, 0, 0);
}}
""")
            host.append(f"""		case CHIMERA_GL_OP_{name}:
			/* declined: see HOST_IGNORES in the generator */
			(void)a;
			return 0;""")
            continue

        if name in STRING_RETURNING:
            # the guest supplies the buffer; see STRING_RETURNING above
            guest.append(f"""static const GLubyte *GLAD_API_PTR w_{name}({signature})
{{
	static char buffer[4096];
	struct {struct} chimera_a;
""" + "".join(f"\tchimera_a.{arg} = {arg};\n" for _, arg in args) + f"""	g_bridge(CHIMERA_GL_OP_{name}, (uint64_t)(uintptr_t)&chimera_a,
		(uint64_t)(uintptr_t)buffer, sizeof buffer, 0, 0);
	return buffer[0] ? (const GLubyte *)buffer : NULL;
}}
""")
            host.append(f"""		case CHIMERA_GL_OP_{name}:
		{{
			struct {struct} *chimera_p = (struct {struct} *)a;
			const char *s = (const char *){name}({call_args});
			char *out = (char *)b;
			if (out && c) {{ if (s) {{ snprintf(out, (size_t)c, "%s", s); }} else {{ out[0] = 0; }} }}
			return 0;
		}}""")
            continue

        body_fill = "".join(f"\tchimera_a.{arg} = {arg};\n" for _, arg in args)
        if ret == "void":
            guest.append(f"""static void GLAD_API_PTR w_{name}({signature})
{{
	struct {struct} chimera_a;
{body_fill}	g_bridge(CHIMERA_GL_OP_{name}, (uint64_t)(uintptr_t)&chimera_a, 0, 0, 0, 0);
}}
""" if args else f"""static void GLAD_API_PTR w_{name}(void)
{{
	g_bridge(CHIMERA_GL_OP_{name}, 0, 0, 0, 0, 0);
}}
""")
            host.append(f"""		case CHIMERA_GL_OP_{name}:
		{{
			{f'struct {struct} *chimera_p = (struct {struct} *)a;' if args else '(void)a;'}
			{name}({call_args});
			return 0;
		}}""")
        else:
            guest.append(f"""static {ret} GLAD_API_PTR w_{name}({signature})
{{
	struct {struct} chimera_a;
{body_fill}	return ({ret})g_bridge(CHIMERA_GL_OP_{name}, (uint64_t)(uintptr_t)&chimera_a, 0, 0, 0, 0);
}}
""" if args else f"""static {ret} GLAD_API_PTR w_{name}(void)
{{
	return ({ret})g_bridge(CHIMERA_GL_OP_{name}, 0, 0, 0, 0, 0);
}}
""")
            host.append(f"""		case CHIMERA_GL_OP_{name}:
		{{
			{f'struct {struct} *chimera_p = (struct {struct} *)a;' if args else '(void)a;'}
			return (uintptr_t){name}({call_args});
		}}""")

    banner = ("/* GENERATED by tools/gen-gl-bridge.py from glad's declarations.\n"
              " * Do not edit; edit the generator or the name list beside it. */\n")

    with open(f"{outdir}/gl-bridge-ops.h", "w") as f:
        f.write(banner + "#pragma once\n\n" + "\n".join(ops) + "\n")

    # A core that loads glad itself (PCSX2 does: gladLoadGL over its context's
    # GetProcAddress) needs the wrappers BY NAME rather than assigned. Going
    # through glad's own loader is what makes its GLAD_GL_* version and
    # extension flags come out of the real driver instead of being invented.
    lookup = ["struct ChimeraGlEntry { const char *name; void *fn; };",
              "static const struct ChimeraGlEntry g_entries[] = {"]
    for n, _, _ in entries:
        lookup.append(f'\t{{ "{n}", (void *)w_{n} }},')
    lookup.append("};")
    lookup.append("""
/* What a glad loader asks for. Unknown names answer null, which is what glad
 * expects for an entry point the driver does not have. */
void *chimera_gl_lookup(const char *name)
{
	for (size_t i = 0; i < sizeof g_entries / sizeof *g_entries; i++)
	{
		if (std::strcmp(g_entries[i].name, name) == 0)
			return g_entries[i].fn;
	}

	return nullptr;
}""")

    with open(f"{outdir}/gl-bridge-guest.cpp", "w") as f:
        f.write(banner + f"""
/* The guest's side: {len(entries)} entry points, each one a struct and a call
 * to the sandbox's single callback. Installing them is the whole of what makes
 * this core's renderer talk to a GPU it cannot see. */
#include <glad/gl.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "gl-bridge.h"
#include "gl-bridge-ops.h"

static chimera_gl_bridge_fn g_bridge;

""" + "\n".join(guest) + f"""
void chimera_gl_install(chimera_gl_bridge_fn bridge)
{{
	g_bridge = bridge;
""" + "".join(f"\tglad_{n} = w_{n};\n" for n, _, _ in entries) + "}\n\n"
                + "\n".join(lookup) + "\n")

    with open(f"{outdir}/gl-bridge-host.inc", "w") as f:
        f.write(banner + "\n".join(host) + "\n")

    print(f"{len(entries)} entry points generated"
          + (f"; {len(missing)} not found in glad: {' '.join(missing)}" if missing else ""))


if __name__ == "__main__":
    main()
