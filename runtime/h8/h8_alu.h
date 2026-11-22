/* H8/300H ALU helpers, mechanically ported from MAME src/devices/cpu/h8/h8.cpp
 * license:BSD-3-Clause  copyright-holders:Olivier Galibert  (re-expressed in C; flags semantics unchanged) */
#ifndef GOLGO_H8_ALU_H
#define GOLGO_H8_ALU_H
#include <stdint.h>
#include "h8_state.h"
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
#define u8(x)  ((uint8_t)(x))
#define u16(x) ((uint16_t)(x))
#define u32(x) ((uint32_t)(x))
#define u64(x) ((uint64_t)(x))
#define s8(x)  ((int8_t)(x))
#define s16(x) ((int16_t)(x))
#define s32(x) ((int32_t)(x))
enum { F_I = 0x80, F_UI = 0x40, F_H = 0x20, F_U = 0x10, F_N = 0x08, F_Z = 0x04, F_V = 0x02, F_C = 0x01 };

static inline uint8_t do_addx8(h8_t *h, u8 v1, u8 v2)
{
	u8 c = h->ccr & F_C ? 1 : 0;
	u16 res = v1 + v2 + c;
	h->ccr &= ~(F_N|F_V|F_C);
	if(1) {
		if(((v1 & 0xf) + (v2 & 0xf) + c) & 0x10)
			h->ccr |= F_H;
		else
			h->ccr &= ~F_H;
	}
	if(u8(res))
		h->ccr &= ~F_Z;
	if(s8(res) < 0)
		h->ccr |= F_N;
	if(~(v1^v2) & (v1^res) & 0x80)
		h->ccr |= F_V;
	if(res & 0x100)
		h->ccr |= F_C;
	return res;
}

static inline uint8_t do_subx8(h8_t *h, u8 v1, u8 v2)
{
	u8 c = h->ccr & F_C ? 1 : 0;
	u16 res = v1 - v2 - c;
	h->ccr &= ~(F_N|F_V|F_C);
	if(1) {
		if(((v1 & 0xf) - (v2 & 0xf) - c) & 0x10)
			h->ccr |= F_H;
		else
			h->ccr &= ~F_H;
	}
	if(u8(res))
		h->ccr &= ~F_Z;
	if(s8(res) < 0)
		h->ccr |= F_N;
	if((v1^v2) & (v1^res) & 0x80)
		h->ccr |= F_V;
	if(res & 0x100)
		h->ccr |= F_C;
	return res;
}

static inline uint8_t do_inc8(h8_t *h, u8 v1, u8 v2)
{
	u8 res = v1 + v2;
	h->ccr &= ~(F_N|F_V|F_Z);
	if(!res)
		h->ccr |= F_Z;
	else if(s8(res) < 0)
		h->ccr |= F_N;
	if(~(v1^v2) & (v1^res) & 0x80)
		h->ccr |= F_V;
	return res;
}

static inline uint16_t do_inc16(h8_t *h, u16 v1, u16 v2)
{
	u16 res = v1 + v2;
	h->ccr &= ~(F_N|F_V|F_Z);
	if(!res)
		h->ccr |= F_Z;
	else if(s16(res) < 0)
		h->ccr |= F_N;
	if(~(v1^v2) & (v1^res) & 0x8000)
		h->ccr |= F_V;
	return res;
}

static inline uint32_t do_inc32(h8_t *h, u32 v1, u32 v2)
{
	u32 res = v1 + v2;
	h->ccr &= ~(F_N|F_V|F_Z);
	if(!res)
		h->ccr |= F_Z;
	else if(s32(res) < 0)
		h->ccr |= F_N;
	if(~(v1^v2) & (v1^res) & 0x80000000)
		h->ccr |= F_V;
	return res;
}

static inline uint8_t do_add8(h8_t *h, u8 v1, u8 v2)
{
	u16 res = v1 + v2;
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(1) {
		if(((v1 & 0xf) + (v2 & 0xf)) & 0x10)
			h->ccr |= F_H;
		else
			h->ccr &= ~F_H;
	}
	if(!u8(res))
		h->ccr |= F_Z;
	else if(s8(res) < 0)
		h->ccr |= F_N;
	if(~(v1^v2) & (v1^res) & 0x80)
		h->ccr |= F_V;
	if(res & 0x100)
		h->ccr |= F_C;
	return res;
}

static inline uint16_t do_add16(h8_t *h, u16 v1, u16 v2)
{
	u32 res = v1 + v2;
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(1) {
		if(((v1 & 0xfff) + (v2 & 0xfff)) & 0x1000)
			h->ccr |= F_H;
		else
			h->ccr &= ~F_H;
	}
	if(!u16(res))
		h->ccr |= F_Z;
	else if(s16(res) < 0)
		h->ccr |= F_N;
	if(~(v1^v2) & (v1^res) & 0x8000)
		h->ccr |= F_V;
	if(res & 0x10000)
		h->ccr |= F_C;
	return res;
}

static inline uint32_t do_add32(h8_t *h, u32 v1, u32 v2)
{
	u64 res = u64(v1) + u64(v2);
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(1) {
		if(((v1 & 0xfffffff) + (v2 & 0xfffffff)) & 0x10000000)
			h->ccr |= F_H;
		else
			h->ccr &= ~F_H;
	}
	if(!u32(res))
		h->ccr |= F_Z;
	else if(s32(res) < 0)
		h->ccr |= F_N;
	if(~(v1^v2) & (v1^res) & 0x80000000)
		h->ccr |= F_V;
	if(res & 0x100000000ULL)
		h->ccr |= F_C;
	return res;
}

static inline uint8_t do_dec8(h8_t *h, u8 v1, u8 v2)
{
	u8 res = v1 - v2;
	h->ccr &= ~(F_N|F_V|F_Z);
	if(!res)
		h->ccr |= F_Z;
	else if(s8(res) < 0)
		h->ccr |= F_N;
	if((v1^v2) & (v1^res) & 0x80)
		h->ccr |= F_V;
	return res;
}

static inline uint16_t do_dec16(h8_t *h, u16 v1, u16 v2)
{
	u16 res = v1 - v2;
	h->ccr &= ~(F_N|F_V|F_Z);
	if(!res)
		h->ccr |= F_Z;
	else if(s16(res) < 0)
		h->ccr |= F_N;
	if((v1^v2) & (v1^res) & 0x8000)
		h->ccr |= F_V;
	return res;
}

static inline uint32_t do_dec32(h8_t *h, u32 v1, u32 v2)
{
	u32 res = v1 - v2;
	h->ccr &= ~(F_N|F_V|F_Z);
	if(!res)
		h->ccr |= F_Z;
	else if(s32(res) < 0)
		h->ccr |= F_N;
	if((v1^v2) & (v1^res) & 0x80000000)
		h->ccr |= F_V;
	return res;
}

static inline uint8_t do_sub8(h8_t *h, u8 v1, u8 v2)
{
	u16 res = v1 - v2;
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(1) {
		if(((v1 & 0xf) - (v2 & 0xf)) & 0x10)
			h->ccr |= F_H;
		else
			h->ccr &= ~F_H;
	}
	if(!u8(res))
		h->ccr |= F_Z;
	else if(s8(res) < 0)
		h->ccr |= F_N;
	if((v1^v2) & (v1^res) & 0x80)
		h->ccr |= F_V;
	if(res & 0x100)
		h->ccr |= F_C;
	return res;
}

static inline uint16_t do_sub16(h8_t *h, u16 v1, u16 v2)
{
	u32 res = v1 - v2;
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(1) {
		if(((v1 & 0xfff) - (v2 & 0xfff)) & 0x1000)
			h->ccr |= F_H;
		else
			h->ccr &= ~F_H;
	}
	if(!u16(res))
		h->ccr |= F_Z;
	else if(s16(res) < 0)
		h->ccr |= F_N;
	if((v1^v2) & (v1^res) & 0x8000)
		h->ccr |= F_V;
	if(res & 0x10000)
		h->ccr |= F_C;
	return res;
}

static inline uint32_t do_sub32(h8_t *h, u32 v1, u32 v2)
{
	u64 res = u64(v1) - u64(v2);
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(1) {
		if(((v1 & 0xfffffff) - (v2 & 0xfffffff)) & 0x10000000)
			h->ccr |= F_H;
		else
			h->ccr &= ~F_H;
	}
	if(!u32(res))
		h->ccr |= F_Z;
	else if(s32(res) < 0)
		h->ccr |= F_N;
	if((v1^v2) & (v1^res) & 0x80000000)
		h->ccr |= F_V;
	if(res & 0x100000000ULL)
		h->ccr |= F_C;
	return res;
}

static inline uint8_t do_shal8(h8_t *h, u8 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x80)
		h->ccr |= F_C;
	if((v & 0xc0) == 0x40 || (v & 0xc0) == 0x80)
		h->ccr |= F_V;
	v <<= 1;
	if(!v)
		h->ccr |= F_Z;
	else if(s8(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint16_t do_shal16(h8_t *h, u16 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x8000)
		h->ccr |= F_C;
	if((v & 0xc000) == 0x4000 || (v & 0xc000) == 0x8000)
		h->ccr |= F_V;
	v <<= 1;
	if(!v)
		h->ccr |= F_Z;
	else if(s16(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint32_t do_shal32(h8_t *h, u32 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x80000000)
		h->ccr |= F_C;
	if((v & 0xc0000000) == 0x40000000 || (v & 0xc0000000) == 0x80000000)
		h->ccr |= F_V;
	v <<= 1;
	if(!v)
		h->ccr |= F_Z;
	else if(s32(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint8_t do_shar8(h8_t *h, u8 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 1)
		h->ccr |= F_C;
	v >>= 1;
	if(!v)
		h->ccr |= F_Z;
	else if(v & 0x40) {
		v |= 0x80;
		h->ccr |= F_N;
	}
	return v;
}

static inline uint16_t do_shar16(h8_t *h, u16 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 1)
		h->ccr |= F_C;
	v >>= 1;
	if(!v)
		h->ccr |= F_Z;
	else if(v & 0x4000) {
		v |= 0x8000;
		h->ccr |= F_N;
	}
	return v;
}

static inline uint32_t do_shar32(h8_t *h, u32 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 1)
		h->ccr |= F_C;
	v >>= 1;
	if(!v)
		h->ccr |= F_Z;
	else if(v & 0x40000000) {
		v |= 0x80000000;
		h->ccr |= F_N;
	}
	return v;
}

static inline uint8_t do_shll8(h8_t *h, u8 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x80)
		h->ccr |= F_C;
	v <<= 1;
	if(!v)
		h->ccr |= F_Z;
	else if(s8(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint16_t do_shll16(h8_t *h, u16 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x8000)
		h->ccr |= F_C;
	v <<= 1;
	if(!v)
		h->ccr |= F_Z;
	else if(s16(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint32_t do_shll32(h8_t *h, u32 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x80000000)
		h->ccr |= F_C;
	v <<= 1;
	if(!v)
		h->ccr |= F_Z;
	else if(s32(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint8_t do_shlr8(h8_t *h, u8 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 1)
		h->ccr |= F_C;
	v >>= 1;
	if(!v)
		h->ccr |= F_Z;
	return v;
}

static inline uint16_t do_shlr16(h8_t *h, u16 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 1)
		h->ccr |= F_C;
	v >>= 1;
	if(!v)
		h->ccr |= F_Z;
	return v;
}

static inline uint32_t do_shlr32(h8_t *h, u32 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 1)
		h->ccr |= F_C;
	v >>= 1;
	if(!v)
		h->ccr |= F_Z;
	return v;
}

static inline uint8_t do_shal2_8(h8_t *h, u8 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x40)
		h->ccr |= F_C;
	if((v & 0xc0) == 0x40 || (v & 0xc0) == 0x80 ||
		(v & 0x60) == 0x20 || (v & 0x60) == 0x40)
		h->ccr |= F_V;
	v <<= 2;
	if(!v)
		h->ccr |= F_Z;
	else if(s8(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint16_t do_shal2_16(h8_t *h, u16 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x4000)
		h->ccr |= F_C;
	if((v & 0xc000) == 0x4000 || (v & 0xc000) == 0x8000 ||
		(v & 0x6000) == 0x2000 || (v & 0x6000) == 0x4000)
		h->ccr |= F_V;
	v <<= 2;
	if(!v)
		h->ccr |= F_Z;
	else if(s16(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint32_t do_shal2_32(h8_t *h, u32 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x40000000)
		h->ccr |= F_C;
	if((v & 0xc0000000) == 0x40000000 || (v & 0xc0000000) == 0x80000000 ||
		(v & 0x60000000) == 0x20000000 || (v & 0x60000000) == 0x40000000)
		h->ccr |= F_V;
	v <<= 2;
	if(!v)
		h->ccr |= F_Z;
	else if(s32(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint8_t do_shar2_8(h8_t *h, u8 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 2)
		h->ccr |= F_C;
	v >>= 2;
	if(!v)
		h->ccr |= F_Z;
	else if(v & 0x20) {
		v |= 0xc0;
		h->ccr |= F_N;
	}
	return v;
}

static inline uint16_t do_shar2_16(h8_t *h, u16 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 2)
		h->ccr |= F_C;
	v >>= 2;
	if(!v)
		h->ccr |= F_Z;
	else if(v & 0x2000) {
		v |= 0xc000;
		h->ccr |= F_N;
	}
	return v;
}

static inline uint32_t do_shar2_32(h8_t *h, u32 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 2)
		h->ccr |= F_C;
	v >>= 2;
	if(!v)
		h->ccr |= F_Z;
	else if(v & 0x20000000) {
		v |= 0xc0000000;
		h->ccr |= F_N;
	}
	return v;
}

static inline uint8_t do_shll2_8(h8_t *h, u8 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x40)
		h->ccr |= F_C;
	v <<= 2;
	if(!v)
		h->ccr |= F_Z;
	else if(s8(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint16_t do_shll2_16(h8_t *h, u16 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x4000)
		h->ccr |= F_C;
	v <<= 2;
	if(!v)
		h->ccr |= F_Z;
	else if(s16(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint32_t do_shll2_32(h8_t *h, u32 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x40000000)
		h->ccr |= F_C;
	v <<= 2;
	if(!v)
		h->ccr |= F_Z;
	else if(s32(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint8_t do_shlr2_8(h8_t *h, u8 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 2)
		h->ccr |= F_C;
	v >>= 2;
	if(!v)
		h->ccr |= F_Z;
	return v;
}

static inline uint16_t do_shlr2_16(h8_t *h, u16 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 2)
		h->ccr |= F_C;
	v >>= 2;
	if(!v)
		h->ccr |= F_Z;
	return v;
}

static inline uint32_t do_shlr2_32(h8_t *h, u32 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 2)
		h->ccr |= F_C;
	v >>= 2;
	if(!v)
		h->ccr |= F_Z;
	return v;
}

static inline uint8_t do_rotl8(h8_t *h, u8 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x80)
		h->ccr |= F_C;
	v = (v << 1) | (v >> 7);
	if(!v)
		h->ccr |= F_Z;
	else if(s8(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint16_t do_rotl16(h8_t *h, u16 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x8000)
		h->ccr |= F_C;
	v = (v << 1) | (v >> 15);
	if(!v)
		h->ccr |= F_Z;
	else if(s16(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint32_t do_rotl32(h8_t *h, u32 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x80000000)
		h->ccr |= F_C;
	v = (v << 1) | (v >> 31);
	if(!v)
		h->ccr |= F_Z;
	else if(s32(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint8_t do_rotr8(h8_t *h, u8 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x01)
		h->ccr |= F_C;
	v = (v << 7) | (v >> 1);
	if(!v)
		h->ccr |= F_Z;
	else if(s8(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint16_t do_rotr16(h8_t *h, u16 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x0001)
		h->ccr |= F_C;
	v = (v << 15) | (v >> 1);
	if(!v)
		h->ccr |= F_Z;
	else if(s16(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint32_t do_rotr32(h8_t *h, u32 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x00000001)
		h->ccr |= F_C;
	v = (v << 31) | (v >> 1);
	if(!v)
		h->ccr |= F_Z;
	else if(s32(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint8_t do_rotxl8(h8_t *h, u8 v)
{
	u8 c = h->ccr & F_C ? 1 : 0;
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x80)
		h->ccr |= F_C;
	v = (v << 1) | c;
	if(!v)
		h->ccr |= F_Z;
	else if(s8(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint16_t do_rotxl16(h8_t *h, u16 v)
{
	u16 c = h->ccr & F_C ? 1 : 0;
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x8000)
		h->ccr |= F_C;
	v = (v << 1) | c;
	if(!v)
		h->ccr |= F_Z;
	else if(s16(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint32_t do_rotxl32(h8_t *h, u32 v)
{
	u32 c = h->ccr & F_C ? 1 : 0;
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x80000000)
		h->ccr |= F_C;
	v = (v << 1) | c;
	if(!v)
		h->ccr |= F_Z;
	else if(s32(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint8_t do_rotxr8(h8_t *h, u8 v)
{
	u8 c = h->ccr & F_C ? 1 : 0;
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x01)
		h->ccr |= F_C;
	v = (v >> 1) | (c << 7);
	if(!v)
		h->ccr |= F_Z;
	else if(s8(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint16_t do_rotxr16(h8_t *h, u16 v)
{
	u8 c = h->ccr & F_C ? 1 : 0;
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x0001)
		h->ccr |= F_C;
	v = (v >> 1) | (c << 15);
	if(!v)
		h->ccr |= F_Z;
	else if(s16(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint32_t do_rotxr32(h8_t *h, u32 v)
{
	u8 c = h->ccr & F_C ? 1 : 0;
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x00000001)
		h->ccr |= F_C;
	v = (v >> 1) | (c << 31);
	if(!v)
		h->ccr |= F_Z;
	else if(s32(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint8_t do_rotl2_8(h8_t *h, u8 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x40)
		h->ccr |= F_C;
	v = (v << 2) | (v >> 6);
	if(!v)
		h->ccr |= F_Z;
	else if(s8(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint16_t do_rotl2_16(h8_t *h, u16 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x4000)
		h->ccr |= F_C;
	v = (v << 2) | (v >> 14);
	if(!v)
		h->ccr |= F_Z;
	else if(s16(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint32_t do_rotl2_32(h8_t *h, u32 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x40000000)
		h->ccr |= F_C;
	v = (v << 2) | (v >> 30);
	if(!v)
		h->ccr |= F_Z;
	else if(s32(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint8_t do_rotr2_8(h8_t *h, u8 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x02)
		h->ccr |= F_C;
	v = (v << 6) | (v >> 2);
	if(!v)
		h->ccr |= F_Z;
	else if(s8(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint16_t do_rotr2_16(h8_t *h, u16 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x0002)
		h->ccr |= F_C;
	v = (v << 14) | (v >> 2);
	if(!v)
		h->ccr |= F_Z;
	else if(s16(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint32_t do_rotr2_32(h8_t *h, u32 v)
{
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x00000002)
		h->ccr |= F_C;
	v = (v << 30) | (v >> 2);
	if(!v)
		h->ccr |= F_Z;
	else if(s32(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint8_t do_rotxl2_8(h8_t *h, u8 v)
{
	u8 c = h->ccr & F_C ? 1 : 0;
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x40)
		h->ccr |= F_C;
	v = (v << 2) | (c << 1) | (v >> 7);
	if(!v)
		h->ccr |= F_Z;
	else if(s8(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint16_t do_rotxl2_16(h8_t *h, u16 v)
{
	u16 c = h->ccr & F_C ? 1 : 0;
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x4000)
		h->ccr |= F_C;
	v = (v << 2) | (c << 1) | (v >> 15);
	if(!v)
		h->ccr |= F_Z;
	else if(s16(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint32_t do_rotxl2_32(h8_t *h, u32 v)
{
	u32 c = h->ccr & F_C ? 1 : 0;
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x40000000)
		h->ccr |= F_C;
	v = (v << 2) | (c << 1) | (v >> 31);
	if(!v)
		h->ccr |= F_Z;
	else if(s32(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint8_t do_rotxr2_8(h8_t *h, u8 v)
{
	u8 c = h->ccr & F_C ? 1 : 0;
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x02)
		h->ccr |= F_C;
	v = (v >> 2) | (c << 6) | (v << 7);
	if(!v)
		h->ccr |= F_Z;
	else if(s8(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint16_t do_rotxr2_16(h8_t *h, u16 v)
{
	u16 c = h->ccr & F_C ? 1 : 0;
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x0002)
		h->ccr |= F_C;
	v = (v >> 2) | (c << 14) | (v << 15);
	if(!v)
		h->ccr |= F_Z;
	else if(s16(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline uint32_t do_rotxr2_32(h8_t *h, u32 v)
{
	u32 c = h->ccr & F_C ? 1 : 0;
	h->ccr &= ~(F_N|F_V|F_Z|F_C);
	if(v & 0x00000002)
		h->ccr |= F_C;
	v = (v >> 2) | (c << 30) | (v << 31);
	if(!v)
		h->ccr |= F_Z;
	else if(s32(v) < 0)
		h->ccr |= F_N;
	return v;
}

static inline void set_nzv8(h8_t *h, u8 v)
{
	h->ccr &= ~(F_N|F_V|F_Z);
	if(!v)
		h->ccr |= F_Z;
	else if(s8(v) < 0)
		h->ccr |= F_N;
}

static inline void set_nzv16(h8_t *h, u16 v)
{
	h->ccr &= ~(F_N|F_V|F_Z);
	if(!v)
		h->ccr |= F_Z;
	else if(s16(v) < 0)
		h->ccr |= F_N;
}

static inline void set_nzv32(h8_t *h, u32 v)
{
	h->ccr &= ~(F_N|F_V|F_Z);
	if(!v)
		h->ccr |= F_Z;
	else if(s32(v) < 0)
		h->ccr |= F_N;
}

static inline void set_nz16(h8_t *h, u16 v)
{
	h->ccr &= ~(F_N|F_Z);
	if(!v)
		h->ccr |= F_Z;
	else if(s16(v) < 0)
		h->ccr |= F_N;
}

static inline void set_nz32(h8_t *h, u32 v)
{
	h->ccr &= ~(F_N|F_Z);
	if(!v)
		h->ccr |= F_Z;
	else if(s32(v) < 0)
		h->ccr |= F_N;
}

#endif
