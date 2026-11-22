// Minimal shim so MAME's gte.cpp (BSD-3, smf) compiles unchanged as a reference in tests only.
#pragma once
#include <cstdint>
#include <cstddef>
#include <bit>
#define ATTR_PRINTF(a,b)
typedef union {
	struct { uint8_t l, h, h2, h3; } b;
	struct { uint16_t l, h; } w;
	struct { int16_t l, h; } sw;
	uint32_t d; int32_t sd;
} PAIR;
namespace util { template <typename T> constexpr T sext(T x, unsigned b) { return T(int64_t(uint64_t(x) << (64 - b)) >> (64 - b)); } }
