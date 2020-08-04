#pragma once

#if !TARGET_PRIZM
#include <stdint.h>
#include <assert.h>
#else
#define uint32_t uint32
#define int32_t int32
#define uint16_t uint16
#define int16_t int16
#define uint8_t unsigned char
#define int8_t int8
#endif

#include "stdio.h"
#include "string.h"
#include "stdlib.h"

#define interrupt

#ifndef __cplusplus
typedef uint8_t bool;
#endif

typedef signed int      int24_t;
typedef unsigned int    uint24_t;
