// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#pragma once
// clang-format off

#include <OSL/oslconfig.h>

#if defined(OSL_REMOVE_PRE_USTRINGHASH)
#   error "include <OSL/device_string.h> should be replaced with <OSL/hashes.h>"
#elif defined(OSL_DEPRECATE_PRE_USTRINGHASH)
#   warning "include <OSL/device_string.h> should be replaced with <OSL/hashes.h>"
#else
#   include <OSL/hashes.h>
#endif

OSL_NAMESPACE_ENTER

// Two old synonyms for ustringrep. We may deprecate these in the future.
using StringParam = ustringrep;
using DeviceString = ustringrep;


#ifndef OSL_REMOVE_PRE_USTRINGHASH
#    define STRING_PARAMS(x) OSL::Hashes::x

inline OSL_HOSTDEVICE ustringhash HDSTR(ustringhash u) { return u; }
inline OSL_HOSTDEVICE ustringhash HDSTR(ustringhash::hash_t u) { return ustringhash(u); }
#ifndef __CUDA_ARCH__
inline OSL_HOSTDEVICE ustringhash HDSTR(ustring u) { return u; }
#endif


#endif  // !defined OSL_REMOVE_PRE_USTRINGHASH

OSL_NAMESPACE_EXIT
// clang-format on
