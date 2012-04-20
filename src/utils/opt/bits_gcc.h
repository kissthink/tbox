/*!The Treasure Box Library
 * 
 * TBox is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 * 
 * TBox is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with TBox; 
 * If not, see <a href="http://www.gnu.org/licenses/"> http://www.gnu.org/licenses/</a>
 * 
 * Copyright (C) 2009 - 2012, ruki All rights reserved.
 *
 * @author		ruki
 * @file		bits_x86.h
 *
 */
#ifndef TB_UTILS_OPT_BITS_GCC_H
#define TB_UTILS_OPT_BITS_GCC_H

/* ///////////////////////////////////////////////////////////////////////
 * includes
 */
#include "prefix.h"

/* ///////////////////////////////////////////////////////////////////////
 * macros
 */
#ifndef TB_CONFIG_COMPILER_NOT_SUPPORT_BUILTIN_FUNCTIONS

// swap 
#define tb_bits_swap_u32(x) 		__builtin_bswap32(x)
#define tb_bits_swap_u64(x) 		__builtin_bswap64(x)

// cl0
#define tb_bits_cl0_u32_be(x) 		((x)? (tb_size_t)__builtin_clz((tb_uint32_t)(x)) : 32)
#define tb_bits_cl0_u32_le(x) 		((x)? (tb_size_t)__builtin_ctz((tb_uint32_t)(x)) : 32)
#if TB_CPU_BITSIZE == 64
#	define tb_bits_cl0_u64_be(x) 	((x)? (tb_size_t)__builtin_clzl((tb_uint64_t)(x)) : 64)
#	define tb_bits_cl0_u64_le(x) 	((x)? (tb_size_t)__builtin_ctzl((tb_uint64_t)(x)) : 64)
#endif

// cb1
#define tb_bits_cb1_u32(x) 			((x)? (tb_size_t)__builtin_popcount((tb_uint32_t)(x)) : 0)
#if TB_CPU_BITSIZE == 64
# 	define tb_bits_cb1_u64(x) 		((x)? (tb_size_t)__builtin_popcountl((tb_uint64_t)(x)) : 0)
#endif

// fb1
#define tb_bits_fb1_u32_le(x) 		((x)? (tb_size_t)__builtin_ffs((tb_uint32_t)(x)) - 1 : 32)
#if TB_CPU_BITSIZE == 64
# 	define tb_bits_fb1_u64_le(x) 	((x)? (tb_size_t)__builtin_ffs((tb_uint64_t)(x)) - 1 : 64)
#endif

#endif // TB_CONFIG_COMPILER_NOT_SUPPORT_BUILTIN_FUNCTIONS

#endif

