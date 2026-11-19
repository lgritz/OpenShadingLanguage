// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

// "oso" fuzz target: treat each input as compiled shader bytecode (.oso),
// load it, build a one-layer shader group from it, and run the runtime
// optimizer on that group.

#include <cstdlib>
#include <memory>
#include <string>

#include <OSL/oslexec.h>
#include <OSL/rendererservices.h>

#include "fuzz_targets.h"


namespace {

// Error handler that discards OSL's messages: nearly every fuzz input is
// malformed, and printing the resulting errors would dominate run time and
// flood CI logs. Set OSL_FUZZ_VERBOSE=1 to see them, e.g. when replaying a
// single reproducer.
class QuietErrorHandler final : public OSL::ErrorHandler {
public:
    QuietErrorHandler()
    {
        const char* v = getenv("OSL_FUZZ_VERBOSE");
        m_verbose     = v && *v && std::string(v) != "0";
    }
    void operator()(int errcode, const std::string& msg) override
    {
        if (m_verbose)
            OSL::ErrorHandler::operator()(errcode, msg);
    }

private:
    bool m_verbose = false;
};


// Process-lifetime objects, created once in oso_init(). Never destroyed:
// libFuzzer exits the process without returning, and tearing down a
// ShadingSystem at exit would only add noise to leak reports.
OSL::RendererServices* renderer = nullptr;
QuietErrorHandler* errhandler   = nullptr;
OSL::ShadingSystem* shadingsys  = nullptr;

// Every input is loaded under this one name. With allow_shader_replacement
// on, each load replaces the previous shader master instead of adding a new
// one, so loaded shaders do not accumulate across iterations.
const char* const shader_name = "fuzz";

}  // namespace



void
oso_init()
{
    // The ShadingSystem requires a RendererServices; the base class defaults
    // are sufficient for loading and runtime optimization.
    renderer   = new OSL::RendererServices;
    errhandler = new QuietErrorHandler;
    shadingsys = new OSL::ShadingSystem(renderer, nullptr, errhandler);
    shadingsys->attribute("allow_shader_replacement", 1);
    shadingsys->attribute("lockgeom", 1);
}



void
oso_run(OSL::cspan<uint8_t> data)
{
    OSL::string_view oso(reinterpret_cast<const char*>(data.data()),
                         data.size());
    if (!shadingsys->LoadMemoryCompiledShader(shader_name, oso))
        return;

    // Build a group with one layer of the loaded shader. The usage string is
    // arbitrary (the first Shader() call sets the group's usage), so
    // "surface" works no matter what kind of shader the .oso declares.
    OSL::ShaderGroupRef group = shadingsys->ShaderGroupBegin(shader_name);
    if (!group)
        return;
    bool ok = shadingsys->Shader(*group, "surface", shader_name, "layer");
    ok &= shadingsys->ShaderGroupEnd(*group);
    if (!ok)
        return;

    // Runtime optimization only, no native code generation: JIT is slow per
    // input, and LLVM is not coverage-instrumented, so the fuzzer would learn
    // little from it.
    shadingsys->optimize_group(group.get(), nullptr, /*do_jit=*/false);

    // group is released here, dropping the last reference to this
    // iteration's shader instance.
}
