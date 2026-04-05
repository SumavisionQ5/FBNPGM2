#pragma once

#ifndef MAME_STUFF_H
#define MAME_STUFF_H


/******************************************************************************
 * typedef
 ******************************************************************************/

#ifndef __mame_typedef__
#define __mame_typedef__
using s8  = signed char;	using u8  = unsigned char;
using s16 = signed short;	using u16 = unsigned short;
using s32 = signed int;		using u32 = unsigned int;
using s64 = signed __int64;	using u64 = unsigned __int64;
#endif	// __mame_typedef__

/******************************************************************************
 * eminline.h
 ******************************************************************************/

/*-------------------------------------------------
	mul_32x32 - perform a signed 32 bit x 32 bit
	multiply and return the full 64 bit result
-------------------------------------------------*/

#ifndef mul_32x32
constexpr s64 mul_32x32(s32 a, s32 b) { return s64(a) * s64(b); }
#endif

/*-------------------------------------------------
	mulu_32x32 - perform an unsigned 32 bit x
	32 bit multiply and return the full 64 bit
	result
-------------------------------------------------*/

#ifndef mulu_32x32
constexpr u64 mulu_32x32(u32 a, u32 b) { return u64(a) * u64(b); }
#endif

/*-------------------------------------------------
	mul_32x32_shift - perform a signed 32 bit x
	32 bit multiply and shift the result by the
	given number of bits before truncating the
	result to 32 bits
-------------------------------------------------*/

#ifndef mul_32x32_shift
constexpr s32 mul_32x32_shift(s32 a, s32 b, u8 shift) {
  return s32((s64(a) * s64(b)) >> shift);
}
#endif

/*-------------------------------------------------
	count_leading_zeros_32 - return the number of
	leading zero bits in a 32-bit value
-------------------------------------------------*/

#ifndef count_leading_zeros_32
inline u8 count_leading_zeros_32(u32 val) {
  if (!val)
    return 32U;
  u8 count;
  for (count = 0; s32(val) >= 0; count++)
    val <<= 1;
  return count;
}
#endif

/*-------------------------------------------------
	population_count_32 - return the number of
	one bits in a 32-bit value
-------------------------------------------------*/

#ifndef population_count_32
inline unsigned population_count_32(u32 val) {
  // optimal Hamming weight assuming fast 32*32->32
  constexpr u32 m1(0x55555555);
  constexpr u32 m2(0x33333333);
  constexpr u32 m4(0x0f0f0f0f);
  constexpr u32 h01(0x01010101);
  val -= (val >> 1) & m1;
  val = (val & m2) + ((val >> 2) & m2);
  val = (val + (val >> 4)) & m4;
  return unsigned((val * h01) >> 24);
}
#endif

/*-------------------------------------------------
	rotr_32 - circularly shift a 32-bit value right
	by the specified number of bits (modulo 32)
-------------------------------------------------*/

#ifndef rotr_32
constexpr u32 rotr_32(u32 val, int shift) {
  shift &= 31;
  if (shift)
    return val >> shift | val << (32 - shift);
  else
    return val;
}
#endif

/******************************************************************************
 * emumem.h / other stuff
 ******************************************************************************/

// Extract a single bit from an integer // #include "bitswap.h"
#define BIT(x, n)				(((x) >> (n)) & 1)

// sign-extending values of arbitrary width (32-bit)
#define sext(value, width)		((s32)((value) << (32 - (width))) >> (32 - (width)))

// helper macro for merging data with the memory mask
#define COMBINE_DATA(varptr)	(*(varptr) = (*(varptr) & ~mem_mask) | (data & mem_mask))

//// I/O line states
//enum line_state
//{
//	CLEAR_LINE = 0,				// clear (a fired or held) line
//	ASSERT_LINE,				// assert an interrupt immediately
//	HOLD_LINE					// hold interrupt line until acknowledged
//};


#if 0
// logging
#define VERBOSE								(0)	// (LOG_COPRO_READS | LOG_COPRO_WRITES | LOG_COPRO_UNKNOWN | LOG_COPRO_RESERVED)
#define LOGMASKED(x, y, ...)	bprintf(0, _T(y), __VA_ARGS__)
#define fatalerror(s, ...)		bprintf(0, _T(s), __VA_ARGS__);

#define LOG_MMU             (1 << 0)
#define LOG_DSP             (1 << 1)
#define LOG_COPRO_READS     (1 << 2)
#define LOG_COPRO_WRITES    (1 << 3)
#define LOG_COPRO_UNKNOWN   (1 << 4)
#define LOG_COPRO_RESERVED  (1 << 5)
#define LOG_TLB             (1 << 6)
#define LOG_TLB_MISS        (1 << 7)
#define LOG_PREFETCH        (1 << 8)

#define LOG_OPS							(1 << 0)
// logging
#endif


#endif // MAME_STUFF_H