// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/imageworks/OpenShadingLanguage

#pragma once

#include <stdint.h>
#include <utility>

//#include "oslexec_pvt.h"

#ifdef __CUDA_ARCH__
#include <optix.h>

OSL_NAMESPACE_ENTER

// Create an OptiX variable for each of the 'standard' strings declared in
// <OSL/strdecls.h>.

namespace DeviceStrings {

#define STATIC_INLINE __device__ __forceinline__ constexpr

// BLATANTLY COPIED FROM ${OIIO}/src/include/OpenImageIO/hash.h
//
#if defined(FARMHASH_UINT128_T_DEFINED)
STATIC_INLINE uint64_t Uint128Low64(const uint128_t x) {
  return static_cast<uint64_t>(x);
}
STATIC_INLINE uint64_t Uint128High64(const uint128_t x) {
  return static_cast<uint64_t>(x >> 64);
}
STATIC_INLINE uint128_t Uint128(uint64_t lo, uint64_t hi) {
  return lo + (((uint128_t)hi) << 64);
}
STATIC_INLINE void CopyUint128(uint128_t &dst, const uint128_t src) { dst = src; }
#else
typedef std::pair<uint64_t, uint64_t> uint128_t;
STATIC_INLINE uint64_t Uint128Low64(const uint128_t x) { return x.first; }
STATIC_INLINE uint64_t Uint128High64(const uint128_t x) { return x.second; }
STATIC_INLINE uint128_t Uint128(uint64_t lo, uint64_t hi) { return uint128_t(lo, hi); }
STATIC_INLINE void CopyUint128(uint128_t &dst, const uint128_t src) { dst.first  = src.first;
                                                                      dst.second = src.second; }
#endif

// This is intended to be a reasonably good hash function.
// May change from time to time, may differ on different platforms, may differ
// depending on NDEBUG.
STATIC_INLINE uint64_t Hash128to64(uint128_t x) {
  // Murmur-inspired hashing.
  const uint64_t kMul = 0x9ddfea08eb382d69ULL;
  uint64_t a = (Uint128Low64(x) ^ Uint128High64(x)) * kMul;
  a ^= (a >> 47);
  uint64_t b = (Uint128High64(x) ^ a) * kMul;
  b ^= (b >> 47);
  b *= kMul;
  return b;
}

// Simple GPU-friendly swap function
template <typename T>
STATIC_INLINE void simpleSwap(T &a, T &b) {
    T c = a;
    a = b;
    b = c;
}


// START OF COPYING FROM ${OIIO}/src/libutil/farmhash.cpp
//
#define NAMESPACE_FOR_HASH_FUNCTIONS OIIO::farmhash

#ifdef _MSC_VER
// Disable pointless windows warning
#pragma warning( disable : 4319 )
#endif


// FARMHASH ASSUMPTIONS: Modify as needed, or use -DFARMHASH_ASSUME_SSE42 etc.
// Note that if you use -DFARMHASH_ASSUME_SSE42 you likely need -msse42
// (or its equivalent for your compiler); if you use -DFARMHASH_ASSUME_AESNI
// you likely need -maes (or its equivalent for your compiler).

#ifdef FARMHASH_ASSUME_SSSE3
#undef FARMHASH_ASSUME_SSSE3
#define FARMHASH_ASSUME_SSSE3 1
#endif

#ifdef FARMHASH_ASSUME_SSE41
#undef FARMHASH_ASSUME_SSE41
#define FARMHASH_ASSUME_SSE41 1
#endif

#ifdef FARMHASH_ASSUME_SSE42
#undef FARMHASH_ASSUME_SSE42
#define FARMHASH_ASSUME_SSE42 1
#endif

#ifdef FARMHASH_ASSUME_AESNI
#undef FARMHASH_ASSUME_AESNI
#define FARMHASH_ASSUME_AESNI 1
#endif

#ifdef FARMHASH_ASSUME_AVX
#undef FARMHASH_ASSUME_AVX
#define FARMHASH_ASSUME_AVX 1
#endif

#if !defined(FARMHASH_CAN_USE_CXX11) && defined(LANG_CXX11)
#define FARMHASH_CAN_USE_CXX11 1
#else
#undef FARMHASH_CAN_USE_CXX11
#define FARMHASH_CAN_USE_CXX11 0
#endif

// FARMHASH PORTABILITY LAYER: Runtime error if misconfigured

#ifndef FARMHASH_DIE_IF_MISCONFIGURED
#define FARMHASH_DIE_IF_MISCONFIGURED do { *(char*)(len % 17) = 0; } while (0)
#endif

// FARMHASH PORTABILITY LAYER: "static inline" or similar

#ifndef STATIC_INLINE
#define STATIC_INLINE static inline
#endif


// FARMHASH PORTABILITY LAYER: endianness and byteswapping functions

#ifdef WORDS_BIGENDIAN
#undef FARMHASH_BIG_ENDIAN
#define FARMHASH_BIG_ENDIAN 1
#endif

#if defined(FARMHASH_LITTLE_ENDIAN) && defined(FARMHASH_BIG_ENDIAN)
#error
#endif

#if !defined(FARMHASH_LITTLE_ENDIAN) && !defined(FARMHASH_BIG_ENDIAN)
#define FARMHASH_UNKNOWN_ENDIAN 1
#endif

#if !defined(bswap_32) || !defined(bswap_64)
#undef bswap_32
#undef bswap_64

#define bswap_32(x) __bswap_32(x)
#define bswap_64(x) __bswap_64(x)

/*
#if defined(HAVE_BUILTIN_BSWAP) || defined(__clang__) ||                \
  (defined(__GNUC__) && ((__GNUC__ == 4 && __GNUC_MINOR__ >= 8) ||      \
                         __GNUC__ >= 5))
// Easy case for bswap: no header file needed.
#define bswap_32(x) __builtin_bswap32(x)
#define bswap_64(x) __builtin_bswap64(x)
#endif
*/
#endif

#if defined(FARMHASH_UNKNOWN_ENDIAN) || !defined(bswap_64)

#undef bswap_32
#undef bswap_64
#include <byteswap.h>

// devices don't seem to have bswap_64() or bswap_32()
template<typename T>
__device__ constexpr T bswap(T v) {
    const int S = sizeof(T);
    T result = 0;

    const T mask = 0xff;
    for (int i = 0; i < S/2; i++) {
        int hi = 8 * (7 - i);
        int lo = 8 * i;
        int shift = 8 * (S - (2*i + 1));
        T lo_mask = mask << lo;
        T hi_mask = mask << hi;
        T new_lo = (v & hi_mask) >> shift;
        T new_hi = (v & lo_mask) << shift;
        result |= (new_lo | new_hi);
    }

    return result;
}
#ifdef WORDS_BIGENDIAN
#define FARMHASH_BIG_ENDIAN 1
#endif

#endif

#ifdef FARMHASH_BIG_ENDIAN
#define uint32_in_expected_order(x) (bswap_32(x))
#define uint64_in_expected_order(x) (bswap_64(x))
#else
#define uint32_in_expected_order(x) (x)
#define uint64_in_expected_order(x) (x)
#endif


// namespace NAMESPACE_FOR_HASH_FUNCTIONS {

namespace farmhash {


STATIC_INLINE uint64_t Fetch64(const char *p) {
  uint64_t result = 0;
  for (size_t i = 0; i < sizeof(result); i++)
      result = (result << 8) + p[sizeof(result) - 1 - i];
  //memcpy(&result, p, sizeof(result));
  return uint64_in_expected_order(result);
}

STATIC_INLINE uint32_t Fetch32(const char *p) {
  uint32_t result = 0;
  for (size_t i = 0; i < sizeof(result); i++)
      result = (result << 8) + p[sizeof(result) - 1 - i];
  //memcpy(&result, p, sizeof(result));
  return uint32_in_expected_order(result);
}

STATIC_INLINE uint32_t Bswap32(uint32_t val) { return bswap<uint32_t>(val); }
STATIC_INLINE uint64_t Bswap64(uint64_t val) { return bswap<uint64_t>(val); }

// FARMHASH PORTABILITY LAYER: bitwise rot

STATIC_INLINE uint32_t BasicRotate32(uint32_t val, int shift) {
  // Avoid shifting by 32: doing so yields an undefined result.
  return shift == 0 ? val : ((val >> shift) | (val << (32 - shift)));
}

STATIC_INLINE uint64_t BasicRotate64(uint64_t val, int shift) {
  // Avoid shifting by 64: doing so yields an undefined result.
  return shift == 0 ? val : ((val >> shift) | (val << (64 - shift)));
}

#if defined(_MSC_VER) && defined(FARMHASH_ROTR)

STATIC_INLINE uint32_t Rotate32(uint32_t val, int shift) {
  return sizeof(unsigned long) == sizeof(val) ?
      _lrotr(val, shift) :
      BasicRotate32(val, shift);
}

STATIC_INLINE uint64_t Rotate64(uint64_t val, int shift) {
  return sizeof(unsigned long) == sizeof(val) ?
      _lrotr(val, shift) :
      BasicRotate64(val, shift);
}

#else

STATIC_INLINE uint32_t Rotate32(uint32_t val, int shift) {
  return BasicRotate32(val, shift);
}
STATIC_INLINE uint64_t Rotate64(uint64_t val, int shift) {
  return BasicRotate64(val, shift);
}

#endif

// }  // namespace NAMESPACE_FOR_HASH_FUNCTIONS
} /*end namespace farmhash*/ 

// FARMHASH PORTABILITY LAYER: debug mode or max speed?
// One may use -DFARMHASH_DEBUG=1 or -DFARMHASH_DEBUG=0 to force the issue.

#define FARMHASH_DEBUG 0 /*OIIO addition to ensure no debug vs opt differences*/

#if !defined(FARMHASH_DEBUG) && (!defined(NDEBUG) || defined(_DEBUG))
#define FARMHASH_DEBUG 1
#endif

#undef debug_mode
#if FARMHASH_DEBUG
#define debug_mode 1
#else
#define debug_mode 0
#endif

// PLATFORM-SPECIFIC FUNCTIONS AND MACROS

#undef x86_64
#if defined (__x86_64) || defined (__x86_64__)
#define x86_64 1
#else
#define x86_64 0
#endif

#undef x86
#if defined(__i386__) || defined(__i386) || defined(__X86__)
#define x86 1
#else
#define x86 x86_64
#endif

#if !defined(is_64bit)
#define is_64bit (x86_64 || (sizeof(void*) == 8))
#endif

#undef can_use_ssse3
#if defined(__SSSE3__) || defined(FARMHASH_ASSUME_SSSE3)

#include <immintrin.h>
#define can_use_ssse3 1
// Now we can use _mm_hsub_epi16 and so on.

#else
#define can_use_ssse3 0
#endif

#undef can_use_sse41
#if defined(__SSE4_1__) || defined(FARMHASH_ASSUME_SSE41)

#include <immintrin.h>
#define can_use_sse41 1
// Now we can use _mm_insert_epi64 and so on.

#else
#define can_use_sse41 0
#endif

#undef can_use_sse42
#if defined(__SSE4_2__) || defined(FARMHASH_ASSUME_SSE42)

#include <nmmintrin.h>
#define can_use_sse42 1
// Now we can use _mm_crc32_u{32,16,8}.  And on 64-bit platforms, _mm_crc32_u64.

#else
#define can_use_sse42 0
#endif

#undef can_use_aesni
#if defined(__AES__) || defined(FARMHASH_ASSUME_AESNI)

#include <wmmintrin.h>
#define can_use_aesni 1
// Now we can use _mm_aesimc_si128 and so on.

#else
#define can_use_aesni 0
#endif

#undef can_use_avx
#if defined(__AVX__) || defined(FARMHASH_ASSUME_AVX)

#include <immintrin.h>
#define can_use_avx 1

#else
#define can_use_avx 0
#endif

// Building blocks for hash functions

// std::swap() was in <algorithm> but is in <utility> from C++11 on.
#if !FARMHASH_CAN_USE_CXX11
#include <algorithm>
#endif


namespace farmhash {

#if can_use_ssse3 || can_use_sse41 || can_use_sse42 || can_use_aesni || can_use_avx
//STATIC_INLINE __m128i Fetch128(const char* s) {
//  return _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
//}
#endif

// Some primes between 2^63 and 2^64 for various uses.
static const uint64_t k0 = 0xc3a5c85c97cb3127ULL;
static const uint64_t k1 = 0xb492b66fbe98f273ULL;
static const uint64_t k2 = 0x9ae16a3b2f90404fULL;

// Magic numbers for 32-bit hashing.  Copied from Murmur3.
static const uint32_t c1 = 0xcc9e2d51;
static const uint32_t c2 = 0x1b873593;

// A 32-bit to 32-bit integer hash copied from Murmur3.
STATIC_INLINE uint32_t fmix(uint32_t h)
{
  h ^= h >> 16;
  h *= 0x85ebca6b;
  h ^= h >> 13;
  h *= 0xc2b2ae35;
  h ^= h >> 16;
  return h;
}

STATIC_INLINE uint32_t Mur(uint32_t a, uint32_t h) {
  // Helper from Murmur3 for combining two 32-bit values.
  a *= c1;
  a = Rotate32(a, 17);
  a *= c2;
  h ^= a;
  h = Rotate32(h, 19);
  return h * 5 + 0xe6546b64;
}

//template <typename T> T DebugTweak(T x) {
template <typename T> __device__ constexpr T DebugTweak(T x) {
  if (debug_mode) {
    if (sizeof(x) == 4) {
      x = ~Bswap32(x * c1);
    } else {
      x = ~Bswap64(x * k1);
    }
  }
  return x;
}

//template <> uint128_t DebugTweak(uint128_t x) {
template <> __device__ constexpr uint128_t DebugTweak(uint128_t x) {
  if (debug_mode) {
    uint64_t y = DebugTweak(Uint128Low64(x));
    uint64_t z = DebugTweak(Uint128High64(x));
    y += z;
    z += y;
    //x = Uint128(y, z * k1);
    CopyUint128(x, Uint128(y, z * k1));
  }
  return x;
}

} /*end namespace farmhash*/

using namespace std;
// using namespace NAMESPACE_FOR_HASH_FUNCTIONS;
using namespace farmhash;
namespace farmhashna {
#undef Fetch
#define Fetch Fetch64

#undef Rotate
#define Rotate Rotate64

#undef Bswap
#define Bswap Bswap64

STATIC_INLINE uint64_t ShiftMix(uint64_t val) {
  return val ^ (val >> 47);
}

STATIC_INLINE uint64_t HashLen16(uint64_t u, uint64_t v) {
  return Hash128to64(Uint128(u, v));
}

STATIC_INLINE uint64_t HashLen16(uint64_t u, uint64_t v, uint64_t mul) {
  // Murmur-inspired hashing.
  uint64_t a = (u ^ v) * mul;
  a ^= (a >> 47);
  uint64_t b = (v ^ a) * mul;
  b ^= (b >> 47);
  b *= mul;
  return b;
}

STATIC_INLINE uint64_t HashLen0to16(const char *s, size_t len) {
  if (len >= 8) {
    uint64_t mul = k2 + len * 2;
    uint64_t a = Fetch(s) + k2;
    uint64_t b = Fetch(s + len - 8);
    uint64_t c = Rotate(b, 37) * mul + a;
    uint64_t d = (Rotate(a, 25) + b) * mul;
    return HashLen16(c, d, mul);
  }
  if (len >= 4) {
    uint64_t mul = k2 + len * 2;
    uint64_t a = Fetch32(s);
    return HashLen16(len + (a << 3), Fetch32(s + len - 4), mul);
  }
  if (len > 0) {
    uint8_t a = s[0];
    uint8_t b = s[len >> 1];
    uint8_t c = s[len - 1];
    uint32_t y = static_cast<uint32_t>(a) + (static_cast<uint32_t>(b) << 8);
    uint32_t z = len + (static_cast<uint32_t>(c) << 2);
    return ShiftMix(y * k2 ^ z * k0) * k2;
  }
  return k2;
}

// This probably works well for 16-byte strings as well, but it may be overkill
// in that case.
STATIC_INLINE uint64_t HashLen17to32(const char *s, size_t len) {
  uint64_t mul = k2 + len * 2;
  uint64_t a = Fetch(s) * k1;
  uint64_t b = Fetch(s + 8);
  uint64_t c = Fetch(s + len - 8) * mul;
  uint64_t d = Fetch(s + len - 16) * k2;
  return HashLen16(Rotate(a + b, 43) + Rotate(c, 30) + d,
                   a + Rotate(b + k2, 18) + c, mul);
}

// Return a 16-byte hash for 48 bytes.  Quick and dirty.
// Callers do best to use "random-looking" values for a and b.
STATIC_INLINE pair<uint64_t, uint64_t> WeakHashLen32WithSeeds(
    uint64_t w, uint64_t x, uint64_t y, uint64_t z, uint64_t a, uint64_t b) {
  a += w;
  b = Rotate(b + a + z, 21);
  uint64_t c = a;
  a += x;
  a += y;
  b += Rotate(a, 44);
  return make_pair(a + z, b + c);
}

// Return a 16-byte hash for s[0] ... s[31], a, and b.  Quick and dirty.
STATIC_INLINE pair<uint64_t, uint64_t> WeakHashLen32WithSeeds(
    const char* s, uint64_t a, uint64_t b) {
  return WeakHashLen32WithSeeds(Fetch(s),
                                Fetch(s + 8),
                                Fetch(s + 16),
                                Fetch(s + 24),
                                a,
                                b);
}

STATIC_INLINE uint64_t H32(const char *s, size_t len, uint64_t mul,
                           uint64_t seed0 = 0, uint64_t seed1 = 0) {
  uint64_t a = Fetch(s) * k1;
  uint64_t b = Fetch(s + 8);
  uint64_t c = Fetch(s + len - 8) * mul;
  uint64_t d = Fetch(s + len - 16) * k2;
  uint64_t u = Rotate(a + b, 43) + Rotate(c, 30) + d + seed0;
  uint64_t v = a + Rotate(b + k2, 18) + c + seed1;
  a = ShiftMix((u ^ v) * mul);
  b = ShiftMix((v ^ a) * mul);
  return b;
}


// Return an 8-byte hash for 33 to 64 bytes.
STATIC_INLINE uint64_t HashLen33to64(const char *s, size_t len) {
  uint64_t mul0 = k2 - 30;
  uint64_t mul1 = k2 - 30 + 2 * len;
  uint64_t h0 = H32(s, 32, mul0);
  uint64_t h1 = H32(s + len - 32, 32, mul1);
  return ((h1 * mul1) + h0) * mul1;
}


/*
// Return an 8-byte hash for 33 to 64 bytes.
STATIC_INLINE uint64_t HashLen33to64(const char *s, size_t len) {
  uint64_t mul = k2 + len * 2;
  uint64_t a = Fetch(s) * k2;
  uint64_t b = Fetch(s + 8);
  uint64_t c = Fetch(s + len - 8) * mul;
  uint64_t d = Fetch(s + len - 16) * k2;
  uint64_t y = Rotate(a + b, 43) + Rotate(c, 30) + d;
  uint64_t z = HashLen16(y, a + Rotate(b + k2, 18) + c, mul);
  uint64_t e = Fetch(s + 16) * mul;
  uint64_t f = Fetch(s + 24);
  uint64_t g = (y + Fetch(s + len - 32)) * mul;
  uint64_t h = (z + Fetch(s + len - 24)) * mul;
  return HashLen16(Rotate(e + f, 43) + Rotate(g, 30) + h,
                   e + Rotate(f + a, 18) + g, mul);
}
*/

STATIC_INLINE uint64_t Hash64(const char *s, size_t len) {
  const uint64_t seed = 81;
  if (len <= 32) {
    if (len <= 16) {
      return HashLen0to16(s, len);
    } else {
      return HashLen17to32(s, len);
    }
  } else if (len <= 64) {
    return HashLen33to64(s, len);
  }

  // For strings over 64 bytes we loop.  Internal state consists of
  // 56 bytes: v, w, x, y, and z.
  uint64_t x = seed;
  uint64_t y = seed * k1 + 113;
  uint64_t z = ShiftMix(y * k2 + 113) * k2;
  pair<uint64_t, uint64_t> v = make_pair(0, 0);
  pair<uint64_t, uint64_t> w = make_pair(0, 0);
  x = x * k2 + Fetch(s);

  // Set end so that after the loop we have 1 to 64 bytes left to process.
  const char* end = s + ((len - 1) / 64) * 64;
  const char* last64 = end + ((len - 1) & 63) - 63;
  assert(s + len - 64 == last64);
  do {
    x = Rotate(x + y + v.first + Fetch(s + 8), 37) * k1;
    y = Rotate(y + v.second + Fetch(s + 48), 42) * k1;
    x ^= w.second;
    y += v.first + Fetch(s + 40);
    z = Rotate(z + w.first, 33) * k1;
    //v = WeakHashLen32WithSeeds(s, v.second * k1, x + w.first);
    CopyUint128(v, WeakHashLen32WithSeeds(s, v.second * k1, x + w.first));
    //w = WeakHashLen32WithSeeds(s + 32, z + w.second, y + Fetch(s + 16));
    CopyUint128(w, WeakHashLen32WithSeeds(s + 32, z + w.second, y + Fetch(s + 16)));
    // The following doesn't work so we'll do our own swapping
    // std::swap(z, x);
    simpleSwap(z, x);
    s += 64;
  } while (s != end);
  uint64_t mul = k1 + ((z & 0xff) << 1);
  // Make s point to the last 64 bytes of input.
  s = last64;
  w.first += ((len - 1) & 63);
  v.first += w.first;
  w.first += v.first;
  x = Rotate(x + y + v.first + Fetch(s + 8), 37) * mul;
  y = Rotate(y + v.second + Fetch(s + 48), 42) * mul;
  x ^= w.second * 9;
  y += v.first * 9 + Fetch(s + 40);
  z = Rotate(z + w.first, 33) * mul;
  //v = WeakHashLen32WithSeeds(s, v.second * mul, x + w.first);
  CopyUint128(v, WeakHashLen32WithSeeds(s, v.second * mul, x + w.first));
  //w = WeakHashLen32WithSeeds(s + 32, z + w.second, y + Fetch(s + 16));
  CopyUint128(w, WeakHashLen32WithSeeds(s + 32, z + w.second, y + Fetch(s + 16)));
  // more swapping woes (see comment above)
  //std::swap(z, x);
  simpleSwap(z, x);
  return HashLen16(HashLen16(v.first, w.first, mul) + ShiftMix(y) * k0 + z,
                   HashLen16(v.second, w.second, mul) + x,
                   mul);
}

} // end namespace farmhashna

//
// END OF COPYING FROM ${OIIO}/src/libutil/farmhash.cpp

STATIC_INLINE size_t mystrlen(const char * s) {
    if (s == nullptr)
        return 0;
    size_t len = 0;
    while (s[len] != 0)
        len++;
    return len;
}

#undef STATIC_INLINE

#if 0
static constexpr size_t Hash(const char* s)
{
  size_t len = mystrlen(s);

  return farmhashna::Hash64(s, len);
}
#endif
}

OSL_NAMESPACE_EXIT


namespace UStringHash {
static constexpr size_t Hash(const char* s)
{
  size_t len = OSL_NAMESPACE::DeviceStrings::mystrlen(s);

  return len ? OSL_NAMESPACE::DeviceStrings::farmhashna::Hash64(s, len)
             : 0;
}
}

#endif // ifef __CUDA_ARCH__
