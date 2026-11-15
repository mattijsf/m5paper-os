// The types and macros of miniz that tinfl needs (miniz_common.h / miniz.h of miniz 3.0.2, reduced).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MINIZ_EXPORT
typedef unsigned char mz_uint8;
typedef signed short mz_int16;
typedef unsigned short mz_uint16;
typedef unsigned int mz_uint32;
typedef unsigned int mz_uint;
typedef int64_t mz_int64;
typedef uint64_t mz_uint64;
typedef int mz_bool;

#define MZ_FALSE (0)
#define MZ_TRUE (1)
#define MZ_MACRO_END while (0, 0)
#define MZ_MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MZ_MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MZ_CLEAR_OBJ(obj) memset(&(obj), 0, sizeof(obj))
#define MZ_CLEAR_ARR(obj) memset((obj), 0, sizeof(obj))
#define MZ_CLEAR_PTR(obj) memset((obj), 0, sizeof(*obj))
#define MZ_ASSERT(x) ((void)0)
#define MZ_MALLOC(x) malloc(x)
#define MZ_FREE(x) free(x)
#define MZ_REALLOC(p, x) realloc(p, x)

#define MZ_UINT16_MAX (0xFFFFU)
#define MZ_UINT32_MAX (0xFFFFFFFFU)

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define MINIZ_LITTLE_ENDIAN 1
#else
#define MINIZ_LITTLE_ENDIAN 0
#endif
#if MINIZ_LITTLE_ENDIAN && (defined(__x86_64__) || defined(__aarch64__) || defined(_M_X64))
#define MINIZ_USE_UNALIGNED_LOADS_AND_STORES 1
#else
#define MINIZ_USE_UNALIGNED_LOADS_AND_STORES 0
#endif
#if defined(__x86_64__) || defined(__aarch64__) || defined(_M_X64)
#define MINIZ_HAS_64BIT_REGISTERS 1
#else
#define MINIZ_HAS_64BIT_REGISTERS 0
#endif

#if MINIZ_USE_UNALIGNED_LOADS_AND_STORES && MINIZ_LITTLE_ENDIAN
#define MZ_READ_LE16(p) *((const mz_uint16 *)(p))
#define MZ_READ_LE32(p) *((const mz_uint32 *)(p))
#else
#define MZ_READ_LE16(p) ((mz_uint32)(((const mz_uint8 *)(p))[0]) | ((mz_uint32)(((const mz_uint8 *)(p))[1]) << 8U))
#define MZ_READ_LE32(p) ((mz_uint32)(((const mz_uint8 *)(p))[0]) | ((mz_uint32)(((const mz_uint8 *)(p))[1]) << 8U) | ((mz_uint32)(((const mz_uint8 *)(p))[2]) << 16U) | ((mz_uint32)(((const mz_uint8 *)(p))[3]) << 24U))
#endif

// The ESP32-S3 ROM contains its own (older, differently sized) tinfl and the linker script binds these names to ROM
// addresses, which would silently replace this copy and overflow our decompressor struct. Own names for ours.
#define tinfl_decompress pos_tinfl_decompress
#define tinfl_decompress_mem_to_heap pos_tinfl_decompress_mem_to_heap
#define tinfl_decompress_mem_to_mem pos_tinfl_decompress_mem_to_mem
#define tinfl_decompress_mem_to_callback pos_tinfl_decompress_mem_to_callback
#define tinfl_decompressor_alloc pos_tinfl_decompressor_alloc
#define tinfl_decompressor_free pos_tinfl_decompressor_free
