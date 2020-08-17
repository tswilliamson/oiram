#pragma once

#include "stdio.h"
#include "string.h"
#include "stdlib.h"

#include "fxcg\display.h"
#include "fxcg\keyboard.h"
#include "fxcg\file.h"
#include "fxcg\registers.h"
#include "fxcg\rtc.h"
#include "fxcg\system.h"
#include "fxcg\serial.h"

#include "ce_sim.h"

typedef signed char int8;
typedef unsigned char uint8;
typedef signed short int16;
typedef unsigned short uint16;
typedef signed int int32;
typedef unsigned int uint32;

#if TARGET_WINSIM
#define ALIGN(x) alignas(x)
#define LITTLE_E
#define FORCE_INLINE __forceinline
#define RESTRICT __restrict
#include <time.h>
#undef LoadImage
#else

#define ALIGN(x) __attribute__((aligned(x)))
#define BIG_E
#define override
#define FORCE_INLINE __attribute__((always_inline)) inline
#define RESTRICT __restrict__
#include "fxcg_registers.h"
#define nullptr NULL

#include "fxcg\heap.h"
#define malloc sys_malloc
#define calloc sys_calloc
#define realloc sys_realloc
#define free sys_free

#endif

#ifdef __cplusplus 
static inline void EndianSwap(unsigned short& s) {
	s = ((s & 0xFF00) >> 8) | ((s & 0x00FF) << 8);
}
static inline void ShortSwap(unsigned int& s) {
	s = ((s & 0xFF00) >> 8) | ((s & 0x00FF) << 8);
}
static inline void EndianSwap(unsigned int& i) {
	i = ((i & 0xFF000000) >> 24) | ((i & 0x00FF0000) >> 8) | ((i & 0x0000FF00) << 8) | ((i & 0x000000FF) << 24);
}
#else
static inline void __EndianSwap16(unsigned short* s) {
	*s = ((*s & 0xFF00) >> 8) | ((*s & 0x00FF) << 8);
}
static inline void __EndianSwap32(unsigned int* i) {
	*i = ((*i & 0xFF000000) >> 24) | ((*i & 0x00FF0000) >> 8) | ((*i & 0x0000FF00) << 8) | ((*i & 0x000000FF) << 24);
}
#endif

#ifdef LITTLE_E
#define EndianSwap_Big EndianSwap
#define ShortSwap_Big ShortSwap
#define EndianSwap_Little(X) {}
#define ShortSwap_Little(X) {}
#define EndianSwap32_Little(X) {}
#define EndianSwap16_Little(X) {}
#else
#define EndianSwap_Big(X) {}
#define ShortSwap_Big(X) {}
#define EndianSwap_Little EndianSwap
#define ShortSwap_Little ShortSwap
// c only
#define EndianSwap32_Little(X) __EndianSwap32(&(X))
#define EndianSwap16_Little(X) __EndianSwap16(&(X))
#endif

// compile time assert, will throw negative subscript error
#ifdef __GNUC__
#define CT_ASSERT(cond) static_assert(cond);
#else
#define CT_ASSERT(cond) typedef char check##__LINE__ [(cond) ? 1 : -1];
#endif

#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif

// #include "ScopeTimer.h"
#ifdef __cplusplus
extern "C" {
#endif

extern void ScreenPrint(char* buffer);
extern void reset_printf();

#ifdef __cplusplus
}
#endif

#define printf(...) { char buffer[256]; memset(buffer, 0, 256); sprintf(buffer, __VA_ARGS__); ScreenPrint(buffer); }
