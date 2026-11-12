#pragma once
/* 主机验证用等价定点运算，替代Helix仅提供的RISC-V/Xtensa内联汇编。 */
#define _ASSEMBLY_H
#include <stdint.h>
typedef int64_t Word64;
static inline int MULSHIFT32(int x, int y) { return (int)(((int64_t)x*y)>>32); }
static inline int FASTABS(int x) { int sign=x>>31; return (int)((uint32_t)(x^sign)-(uint32_t)sign); }
static inline int CLZ(int x) { return x ? __builtin_clz((unsigned)x) : 32; }
static inline Word64 MADD64(Word64 sum, int x, int y) { return sum+(int64_t)x*y; }
static inline Word64 SHL64(Word64 x, int n) { return (Word64)((uint64_t)x<<n); }
static inline Word64 SAR64(Word64 x, int n) { return x>>n; }
