// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage


#include "oslexec_pvt.h"
#include "backendcpp.h"

using namespace OSL;
using namespace OSL::pvt;

OSL_NAMESPACE_BEGIN

namespace pvt {


BackendCpp::BackendCpp(ShadingSystemImpl& shadingsys, ShaderGroup& group,
                         ShadingContext* ctx)
    : OSOProcessorBase(shadingsys, group, ctx)
{
}



BackendCpp::~BackendCpp() {}



};  // namespace pvt
OSL_NAMESPACE_END
