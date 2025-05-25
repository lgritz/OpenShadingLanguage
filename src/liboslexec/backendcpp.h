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
private:
};



};  // namespace pvt
OSL_NAMESPACE_END
