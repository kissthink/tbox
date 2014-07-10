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
 * Copyright (C) 2009 - 2015, ruki All rights reserved.
 *
 * @author      ruki
 * @file        static_large_pool.c
 */

/* //////////////////////////////////////////////////////////////////////////////////////
 * trace
 */
#define TB_TRACE_MODULE_NAME            "static_large_pool"
#define TB_TRACE_MODULE_DEBUG           (1)

/* //////////////////////////////////////////////////////////////////////////////////////
 * includes
 */
#include "static_large_pool.h"

// the static large data head type
typedef __tb_aligned__(TB_POOL_DATA_ALIGN) struct __tb_static_large_data_head_t
{
    // the data space size: the allocated size + left size
    tb_uint32_t                     space : 31;

    // is free?
    tb_uint32_t                     bfree : 1;

    // the data head base
    tb_pool_data_head_t             base;

}__tb_aligned__(TB_POOL_DATA_ALIGN) tb_static_large_data_head_t;

/*! the static large pool impl type
 *
 * <pre>
 *
 * .e.g pagesize == 4KB
 *
 *        --------------------------------------------------------------------------
 *       |                                     data                                 |
 *        --------------------------------------------------------------------------
 *                                              |
 *        --------------------------------------------------------------------------
 * pool: | head | 4KB | 16KB | 8KB | 128KB | ... | 32KB |       ...       |  4KB*N  |
 *        --------------------------------------------------------------------------
 *                       |                       |               |
 *                       |                       `---------------`
 *                       |                        merge free space when alloc or free
 *                       |
 *        ------------------------------------------
 *       | tb_static_large_data_head_t | data space |
 *        ------------------------------------------
 *                                                
 *        -----------------------
 * pred: | <=4KB :      4KB      |
 *       |-----------------------|
 *       | <=8KB :      8KB      |
 *       |-----------------------|
 *       | <=16KB :   12-16KB    |
 *       |-----------------------|
 *       | <=32KB :   20-32KB    |
 *       |-----------------------|
 *       | <=64KB :   36-64KB    |
 *       |-----------------------|
 *       | <=128KB :  68-128KB   |
 *       |-----------------------|
 *       | <=256KB :  132-256KB  |
 *       |-----------------------|
 *       | <=512KB :  260-512KB  |
 *       |-----------------------|
 *       | >512KB :   516-...KB  |
 *        -----------------------
 *
 * </pre>
 */
typedef __tb_aligned__(TB_POOL_DATA_ALIGN) struct __tb_static_large_pool_impl_t
{
    // the data size
    tb_size_t                       data_size;

    // the data head
    tb_static_large_data_head_t*    data_head;

    // the data tail
    tb_static_large_data_head_t*    data_tail;

    // the page size
    tb_size_t                       pagesize;

#ifdef __tb_debug__
    // the peak size
    tb_size_t                       peak_size;

    // the total size
    tb_size_t                       total_size;

    // the occupied size
    tb_size_t                       occupied_size;

    // the malloc count
    tb_size_t                       malloc_count;

    // the ralloc count
    tb_size_t                       ralloc_count;

    // the free count
    tb_size_t                       free_count;
#endif

}__tb_aligned__(TB_POOL_DATA_ALIGN) tb_static_large_pool_impl_t;

/* //////////////////////////////////////////////////////////////////////////////////////
 * checker implementation
 */
#ifdef __tb_debug__
static tb_void_t tb_static_large_pool_check_data(tb_static_large_pool_impl_t* impl, tb_static_large_data_head_t const* data_head)
{
    // check
    tb_assert_and_check_return(impl && data_head);

    // done
    tb_bool_t           ok = tb_false;
    tb_byte_t const*    data = (tb_byte_t const*)&(data_head[1]);
    do
    {
        // check
        tb_assertf_break(!data_head->bfree, "data have been freed: %p", data);
        tb_assertf_break(data_head->base.debug.magic == TB_POOL_DATA_MAGIC, "the invalid data: %p", data);
        tb_assertf_break(((tb_byte_t*)data)[data_head->base.size] == TB_POOL_DATA_PATCH, "data underflow");

        // ok
        ok = tb_true;

    } while (0);

    // failed? dump it
#ifdef __tb_debug__
    if (!ok) 
    {
        // dump data
        tb_pool_data_dump(data, tb_true, "[static_large_pool]: [error]: ");

        // abort
        tb_abort();
    }
#endif
}
static tb_void_t tb_static_large_pool_check_next(tb_static_large_pool_impl_t* impl, tb_static_large_data_head_t const* data_head)
{
    // check
    tb_assert_and_check_return(impl && data_head);

    // check the next data
    tb_static_large_data_head_t* next_head = (tb_static_large_data_head_t*)((tb_byte_t*)&(data_head[1]) + data_head->space);
    if (next_head < impl->data_tail && !next_head->bfree) 
        tb_static_large_pool_check_data(impl, next_head);
}
#endif

/* //////////////////////////////////////////////////////////////////////////////////////
 * malloc implementation
 */
static tb_static_large_data_head_t* tb_static_large_pool_malloc_find(tb_static_large_pool_impl_t* impl, tb_static_large_data_head_t* data_head, tb_size_t walk_size, tb_size_t size)
{
    // check
    tb_assert_and_check_return_val(impl && data_head && size, tb_null);

    // the data tail
    tb_static_large_data_head_t* data_tail = impl->data_tail;
    tb_check_return_val(data_head < data_tail, tb_null);

    // find the free data 
    while ((data_head + 1) <= data_tail && walk_size)
    {
        // the data space size
        tb_size_t space = data_head->space;

        // check the space size
        tb_assert_abort(!((sizeof(tb_static_large_data_head_t) + space) & (impl->pagesize - 1)));
            
#ifdef __tb_debug__
        // check the data
        if (!data_head->bfree) tb_static_large_pool_check_data(impl, data_head);
#endif

        // allocate if the data is free
        if (data_head->bfree)
        {
            // is enough?           
            if (space >= size)
            {
                // split it if the free data is too large
                if (space > sizeof(tb_static_large_data_head_t) + size)
                {
                    // split data_head
                    tb_static_large_data_head_t* next_head = (tb_static_large_data_head_t*)((tb_byte_t*)(data_head + 1) + size);
                    next_head->space = space - size - sizeof(tb_static_large_data_head_t);
                    next_head->bfree = 1;
                    data_head->space = size;
                }
                // use the whole data
                else data_head->space = space;

                // alloc the data 
                data_head->bfree = 0;

                // return the data head
                return data_head;
            }
            else // attempt to merge next free data if this free data is too small
            {
                // the next data head
                tb_static_large_data_head_t* next_head = (tb_static_large_data_head_t*)((tb_byte_t*)(data_head + 1) + space);
            
                // break if doesn't exist next data
                tb_check_break(next_head + 1 < data_tail);

                // the next data is free?
                if (next_head->bfree)
                {
                    // merge next data
                    data_head->space += sizeof(tb_static_large_data_head_t) + next_head->space;

                    // continue handle this data 
                    continue ;
                }
            }
        }

        // walk_size--
        walk_size--;
    
        // skip it if the data is non-free or too small
        data_head = (tb_static_large_data_head_t*)((tb_byte_t*)(data_head + 1) + space);
    }

    // failed
    return tb_null;
}
static tb_static_large_data_head_t* tb_static_large_pool_malloc_done(tb_static_large_pool_impl_t* impl, tb_size_t size, tb_size_t* real __tb_debug_decl__)
{
    // check
    tb_assert_and_check_return_val(impl && impl->data_head, tb_null);

    // done
    tb_bool_t                       ok = tb_false;
    tb_static_large_data_head_t*    data_head = tb_null;
    do
    {
#ifdef __tb_debug__
        // patch 0xcc
        tb_size_t patch = 1;
#else
        tb_size_t patch = 0;
#endif

        // compile the need size for the page alignment
        tb_size_t need = tb_align(size + patch, impl->pagesize) - sizeof(tb_static_large_data_head_t);
        if (size + patch > need) need = tb_align(size + patch + impl->pagesize, impl->pagesize) - sizeof(tb_static_large_data_head_t);

        // TODO: pred
        // ...

        // find the free data from the first data head 
        data_head = tb_static_large_pool_malloc_find(impl, impl->data_head, -1, need);
        tb_check_break(data_head);
        tb_assert_abort(data_head->space >= size + patch);

        // the real size
        tb_size_t size_real = real? (data_head->space - patch) : size;

        // save the real size
        if (real) *real = size_real;
        data_head->base.size = size_real;

#ifdef __tb_debug__
        // init the debug info
        data_head->base.debug.magic     = TB_POOL_DATA_MAGIC;
        data_head->base.debug.file      = file_;
        data_head->base.debug.func      = func_;
        data_head->base.debug.line      = line_;

        // save backtrace
        tb_pool_data_save_backtrace(&data_head->base, 3);

        // make the dirty data and patch 0xcc for checking underflow
        tb_memset((tb_pointer_t)&(data_head[1]), TB_POOL_DATA_PATCH, size_real + patch);
 
        // update the occupied size
        impl->occupied_size += sizeof(tb_static_large_data_head_t) + data_head->space - 1 - sizeof(tb_pool_data_debug_head_t);

        // update the total size
        impl->total_size    += data_head->base.size;

        // update the peak size
        if (impl->total_size > impl->peak_size) impl->peak_size = impl->total_size;

        // update the malloc count
        impl->malloc_count++;
#endif

        // ok
        ok = tb_true;

    } while (0);

    // failed? clear it
    if (!ok) data_head = tb_null;

    // ok?
    return data_head;
}
static tb_static_large_data_head_t* tb_static_large_pool_ralloc_fast(tb_static_large_pool_impl_t* impl, tb_static_large_data_head_t* data_head, tb_size_t size, tb_size_t* real __tb_debug_decl__)
{
    // check
    tb_assert_and_check_return_val(impl && data_head && size, tb_null);

    // done
    tb_bool_t ok = tb_false;
    do
    {
#ifdef __tb_debug__
        // patch 0xcc
        tb_size_t patch = 1;
#else
        tb_size_t patch = 0;
#endif

        // the prev size
        tb_size_t prev_size = data_head->base.size;

        // the prev space
        tb_size_t prev_space = data_head->space;

        // this data space is not enough?
        if (size + patch > data_head->space)
        {
            // attempt to merge the next free data
            tb_static_large_data_head_t* data_tail = impl->data_tail;
            tb_static_large_data_head_t* next_head = (tb_static_large_data_head_t*)((tb_byte_t*)&(data_head[1]) + data_head->space);
            while (next_head < data_tail && next_head->bfree) 
            {
                // merge it
                data_head->space += sizeof(tb_static_large_data_head_t) + next_head->space;

                // the next data head
                next_head = (tb_static_large_data_head_t*)((tb_byte_t*)&(data_head[1]) + data_head->space);
            }
        }

        // enough?
        tb_check_break(size + patch <= data_head->space);

        // the real size
        tb_size_t size_real = real? (data_head->space - patch) : size;

        // save the real size
        if (real) *real = size_real;
        data_head->base.size = size_real;

#ifdef __tb_debug__
        // init the debug info
        data_head->base.debug.magic     = TB_POOL_DATA_MAGIC;
        data_head->base.debug.file      = file_;
        data_head->base.debug.func      = func_;
        data_head->base.debug.line      = line_;

        // save backtrace
        tb_pool_data_save_backtrace(&data_head->base, 3);

        // make the dirty data 
        if (size_real > prev_size) tb_memset((tb_byte_t*)&(data_head[1]) + prev_size, TB_POOL_DATA_PATCH, size_real - prev_size);

        // patch 0xcc for checking underflow
        ((tb_byte_t*)&(data_head[1]))[size_real] = TB_POOL_DATA_PATCH;
 
        // update the occupied size
        impl->occupied_size += data_head->space;
        impl->occupied_size -= prev_space;

        // update the total size
        impl->total_size    += size_real;
        impl->total_size    -= prev_size;

        // update the peak size
        if (impl->total_size > impl->peak_size) impl->peak_size = impl->total_size;
#endif

        // ok
        ok = tb_true;

    } while (0);

    // failed? clear it
    if (!ok) data_head = tb_null;

    // ok?
    return data_head;
}

/* //////////////////////////////////////////////////////////////////////////////////////
 * implementation
 */
tb_large_pool_ref_t tb_static_large_pool_init(tb_byte_t* data, tb_size_t size)
{
    // check
    tb_assert_and_check_return_val(data && size, tb_null);
    tb_assert_static(!(sizeof(tb_static_large_data_head_t) & (TB_POOL_DATA_ALIGN - 1)));
    tb_assert_static(!(sizeof(tb_static_large_pool_impl_t) & (TB_POOL_DATA_ALIGN - 1)));

    // align data and size
    tb_size_t diff = tb_align((tb_size_t)data, TB_POOL_DATA_ALIGN) - (tb_size_t)data;
    tb_assert_and_check_return_val(size > diff + sizeof(tb_static_large_pool_impl_t), tb_null);
    size -= diff;
    data += diff;

    // init pool
    tb_static_large_pool_impl_t* impl = (tb_static_large_pool_impl_t*)data;
    tb_memset(impl, 0, sizeof(tb_static_large_pool_impl_t));

    // init pagesize
    impl->pagesize = tb_page_size();
    tb_assert_and_check_return_val(impl->pagesize, tb_null);

    // init data size
    impl->data_size = size - sizeof(tb_static_large_pool_impl_t);
    tb_assert_and_check_return_val(impl->data_size > impl->pagesize, tb_null);

    // align data size
    impl->data_size = tb_align(impl->data_size - impl->pagesize, impl->pagesize);
    tb_assert_and_check_return_val(impl->data_size > sizeof(tb_static_large_data_head_t), tb_null);

    // init data head 
    impl->data_head = (tb_static_large_data_head_t*)&impl[1];
    impl->data_head->bfree = 1;
    impl->data_head->space = impl->data_size - sizeof(tb_static_large_data_head_t);
    tb_assert_and_check_return_val(!((tb_size_t)impl->data_head & (TB_POOL_DATA_ALIGN - 1)), tb_null);

    // init data tail
    impl->data_tail = (tb_static_large_data_head_t*)((tb_byte_t*)&impl->data_head[1] + impl->data_head->space);

    // ok
    return (tb_large_pool_ref_t)impl;
}
tb_void_t tb_static_large_pool_exit(tb_large_pool_ref_t pool)
{
    // check
    tb_static_large_pool_impl_t* impl = (tb_static_large_pool_impl_t*)pool;
    tb_assert_and_check_return(impl);

    // clear it
    tb_static_large_pool_clear(pool);
}
tb_void_t tb_static_large_pool_clear(tb_large_pool_ref_t pool)
{
    // check
    tb_static_large_pool_impl_t* impl = (tb_static_large_pool_impl_t*)pool;
    tb_assert_and_check_return(impl && impl->data_head && impl->data_size > sizeof(tb_static_large_data_head_t));

    // clear it
    impl->data_head->bfree = 1;
    impl->data_head->space = impl->data_size - sizeof(tb_static_large_data_head_t);
}
tb_pointer_t tb_static_large_pool_malloc(tb_large_pool_ref_t pool, tb_size_t size, tb_size_t* real __tb_debug_decl__)
{
    // check
    tb_static_large_pool_impl_t* impl = (tb_static_large_pool_impl_t*)pool;
    tb_assert_and_check_return_val(impl && size, tb_null);

    // done
    tb_static_large_data_head_t* data_head = tb_static_large_pool_malloc_done(impl, size, real __tb_debug_args__);
    tb_check_return_val(data_head, tb_null);

    // ok
    return (tb_pointer_t)&(data_head[1]);
}
tb_pointer_t tb_static_large_pool_ralloc(tb_large_pool_ref_t pool, tb_pointer_t data, tb_size_t size, tb_size_t* real __tb_debug_decl__)
{
    // check
    tb_static_large_pool_impl_t* impl = (tb_static_large_pool_impl_t*)pool;
    tb_assert_and_check_return_val(impl && data && size, tb_null);

    // done
    tb_bool_t                       ok = tb_false;
    tb_byte_t*                      data_real = tb_null;
    tb_static_large_data_head_t*    data_head = tb_null;
    tb_static_large_data_head_t*    aloc_head = tb_null;
    do
    {
        // the data head
        data_head = &(((tb_static_large_data_head_t*)data)[-1]);
        tb_assertf_and_check_break(!data_head->bfree, "ralloc freed data: %p", data);
        tb_assertf_break(data_head->base.debug.magic == TB_POOL_DATA_MAGIC, "ralloc invalid data: %p", data);
        tb_assertf_and_check_break(data_head >= impl->data_head && data_head < impl->data_tail, "the data: %p not belong to pool: %p", data, pool);
        tb_assertf_break(((tb_byte_t*)data)[data_head->base.size] == TB_POOL_DATA_PATCH, "data underflow");

#ifdef __tb_debug__
        // check the next data
        tb_static_large_pool_check_next(impl, data_head);
#endif

        // attempt to allocate it fastly if enough
        aloc_head = tb_static_large_pool_ralloc_fast(impl, data_head, size, real __tb_debug_args__);
        if (!aloc_head)
        {
            // allocate it
            aloc_head = tb_static_large_pool_malloc_done(impl, size, real __tb_debug_args__);
            tb_check_break(aloc_head);

            // not same?
            if (aloc_head != data_head)
            {
                // copy the real data
                tb_memcpy((tb_pointer_t)&aloc_head[1], data, tb_min(size, data_head->base.size));
                
                // free the previous data
                tb_static_large_pool_free(pool, data __tb_debug_args__);
            }
        }

        // the real data
        data_real = (tb_byte_t*)&aloc_head[1];

#ifdef __tb_debug__
        // update the ralloc count
        impl->ralloc_count++;
#endif

        // ok
        ok = tb_true;

    } while (0);

    // failed? clear it
    if (!ok) data_real = tb_null;

    // ok?
    return (tb_pointer_t)data_real;
}
tb_bool_t tb_static_large_pool_free(tb_large_pool_ref_t pool, tb_pointer_t data __tb_debug_decl__)
{
    // check
    tb_static_large_pool_impl_t* impl = (tb_static_large_pool_impl_t*)pool;
    tb_assert_and_check_return_val(impl && data, tb_false);

    // done
    tb_bool_t                       ok = tb_false;
    tb_static_large_data_head_t*    data_head = tb_null;
    do
    {
        // the data head
        data_head = &(((tb_static_large_data_head_t*)data)[-1]);
        tb_assertf_and_check_break(!data_head->bfree, "double free data: %p", data);
        tb_assertf_break(data_head->base.debug.magic == TB_POOL_DATA_MAGIC, "free invalid data: %p", data);
        tb_assertf_and_check_break(data_head >= impl->data_head && data_head < impl->data_tail, "the data: %p not belong to pool: %p", data, pool);
        tb_assertf_break(((tb_byte_t*)data)[data_head->base.size] == TB_POOL_DATA_PATCH, "data underflow");

#ifdef __tb_debug__
        // check the next data
        tb_static_large_pool_check_next(impl, data_head);

        // update the occupied size
        impl->occupied_size -= sizeof(tb_static_large_data_head_t) + data_head->space - 1 - sizeof(tb_pool_data_debug_head_t);

        // update the total size
        impl->total_size    -= data_head->base.size;

        // update the free count
        impl->free_count++;
#endif

        // attempt merge the next free data
        tb_static_large_data_head_t* next_head = (tb_static_large_data_head_t*)((tb_byte_t*)&(data_head[1]) + data_head->space);
        if (next_head < impl->data_tail && next_head->bfree) 
            data_head->space += sizeof(tb_static_large_data_head_t) + next_head->space;

        // free it
        data_head->bfree = 1;

        // ok
        ok = tb_true;

    } while (0);

    // ok?
    return ok;
}
#ifdef __tb_debug__
tb_void_t tb_static_large_pool_dump(tb_large_pool_ref_t pool)
{
    // check
    tb_static_large_pool_impl_t* impl = (tb_static_large_pool_impl_t*)pool;
    tb_assert_and_check_return(impl);

    // trace
    tb_trace_i("======================================================================");

    // the data head
    tb_static_large_data_head_t* data_head = impl->data_head;
    tb_assert_and_check_return(data_head);

    // the data tail
    tb_static_large_data_head_t* data_tail = impl->data_tail;
    tb_assert_and_check_return(data_tail);

    // done
    tb_size_t frag_count = 0;
    while ((data_head + 1) <= data_tail)
    {
        // non-free?
        if (!data_head->bfree)
        {
            // check it
            tb_static_large_pool_check_data(impl, data_head);

            // trace
            tb_trace_e("leak: %p", &data_head[1]);

            // dump data
            tb_pool_data_dump((tb_byte_t const*)&data_head[1], tb_false, "[static_large_pool]: [error]: ");
        }

        // fragment++
        frag_count++;

        // the next head
        data_head = (tb_static_large_data_head_t*)((tb_byte_t*)(data_head + 1) + data_head->space);
    }

    // trace debug info
    tb_trace_i("peak_size: %lu",            impl->peak_size);
    tb_trace_i("wast_rate: %llu/10000",     impl->occupied_size? (((tb_hize_t)impl->occupied_size - impl->total_size) * 10000) / (tb_hize_t)impl->occupied_size : 0);
    tb_trace_i("frag_count: %lu",           frag_count);
    tb_trace_i("free_count: %lu",           impl->free_count);
    tb_trace_i("malloc_count: %lu",         impl->malloc_count);
    tb_trace_i("ralloc_count: %lu",         impl->ralloc_count);

    // trace
    tb_trace_i("======================================================================");
}
#endif