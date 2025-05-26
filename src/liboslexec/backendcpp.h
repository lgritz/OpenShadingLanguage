// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#pragma once

#include <map>
#include <vector>

#include "oslexec_pvt.h"
using namespace OSL;
using namespace OSL::pvt;


OSL_NAMESPACE_BEGIN

namespace pvt {  // OSL::pvt



/// OSOProcessor that generates LLVM IR and JITs it to give machine
/// code to implement a shader group.
class BackendCpp final : public OSOProcessorBase {
public:
    BackendCpp(ShadingSystemImpl& shadingsys, ShaderGroup& group,
               ShadingContext* context);

    virtual ~BackendCpp();

    virtual void run();

    void build_cpp_code(int opbegin, int opend, bool do_indent_block = true);

    /// output text
    template<typename... Args>
    inline void outputfmt(const char* fmt, Args&&... args) const
    {
        // FIXME: temporary, just echo to the terminal
        fmt::print(fmt, std::forward<Args>(args)...);
    }

    /// output text with newline
    template<typename... Args>
    inline void outputfmtln(const char* fmt, Args&&... args) const
    {
        // FIXME: temporary, just echo to the terminal
        fmt::println(fmt, std::forward<Args>(args)...);
    }

    void indent(int delta);
    void increment_indent() { indent(4); }
    void decrement_indent() { indent(-4); }
    string_view indentstr() const { return m_indentview; }

private:
    int m_indentlevel = 0;
    string_view m_indentview;

    void op_gen_init();
};



};  // namespace pvt
OSL_NAMESPACE_END
