// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#pragma once

#include <OSL/oslconfig.h>

#ifdef __CUDA_ARCH__
#include <optix.h>
#endif

// USAGE NOTES:
//
// To define a "standard" DeviceString, add a STRDECL to <OSL/strdecls.h>
// specifying the string literal and the name to use for the variable.


OSL_NAMESPACE_ENTER


// Strings are stored in a block of CUDA device memory. String variables hold a
// pointer to the start of the char array for each string. Each canonical string
// has a unique entry in the table, so two strings can be tested for equality by
// comparing their addresses.
//
// As a convenience, the ustring hash and the length of the string are also
// stored in the table, in the 16 bytes preceding the characters.

struct DeviceString {
#if defined(__CUDA_ARCH__) && OPTIX_VERSION >= 70000
    size_t      m_chars;
#else
    const char* m_chars;
#endif

#if defined(__CUDA_ARCH__) && OPTIX_VERSION >= 70000
    OSL_HOSTDEVICE DeviceString() {}
    OSL_HOSTDEVICE DeviceString(uint64_t i) : m_chars(i) {}
#endif

    OSL_HOSTDEVICE uint64_t hash() const
    {
#if defined(__CUDA_ARCH__) && OPTIX_VERSION >= 70000
        return m_chars;
#else
        return *(uint64_t*)(m_chars - sizeof(uint64_t) - sizeof(uint64_t));
#endif
    }

    OSL_HOSTDEVICE uint64_t length() const
    {
#if defined(__CUDA_ARCH__) && OPTIX_VERSION >= 70000
        return 0;
#else
        return *(uint64_t*)(m_chars - sizeof(uint64_t));
#endif
    }

    OSL_HOSTDEVICE const char* c_str() const
    {
#if defined(__CUDA_ARCH__) && OPTIX_VERSION >= 70000
        return nullptr;
#else
        return m_chars;
#endif
    }

    OSL_HOSTDEVICE bool operator== (const DeviceString& other) const
    {
        return m_chars == other.m_chars;
    }

    OSL_HOSTDEVICE bool operator!= (const DeviceString& other) const
    {
        return m_chars != other.m_chars;
    }

#if defined(__CUDA_ARCH__) && OPTIX_VERSION >= 70000

    OSL_HOSTDEVICE bool operator== (const size_t other) const
    {
        return hash() == other;
    }

    OSL_HOSTDEVICE bool operator!= (const size_t other) const
    {
        return hash() != other;
    }

    OSL_HOSTDEVICE operator size_t() const { return hash(); }

#endif
};


// Choose the right cast for string parameters depending on the target. The
// macro is the same as the USTR macro defined in oslexec_pvt.h when compiling
// for the host.
#ifndef __CUDA_ARCH__
# define HDSTR(cstr) (*((ustring *)&cstr))
#else
#define HDSTR(cstr) (*((OSL::DeviceString*)&cstr))
#endif


// When compiling shadeops C++ sources for CUDA devices, we need to use
// DeviceString instead of ustring for some input parameters, so we use this
// typedef to select the correct type depending on the target.
#ifndef __CUDA_ARCH__
typedef ustring StringParam;
#else
#if OPTIX_VERSION >= 70000
  typedef DeviceString StringParam;
# else
  typedef DeviceString StringParam;
#endif
#endif


#ifdef __CUDA_ARCH__
namespace DeviceStrings {
#define STRDECL(str,var_name)                       \
    extern __device__ OSL_NAMESPACE::DeviceString var_name;
#if OPTIX_VERSION < 70000
# include <OSL/strdecls.h>
#endif
#undef STRDECL
}
#else
#  define STRING_PARAMS(x)  StringParams::x
#endif


OSL_NAMESPACE_EXIT


#ifdef __CUDA_ARCH__
#if OPTIX_VERSION < 70000
namespace StringParams = OSL_NAMESPACE::DeviceStrings;
#endif
#else
namespace StringParams = OSL_NAMESPACE::Strings;
#endif
