// Copyright (c) 2012, Cornell University
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//     * Redistributions of source code must retain the above copyright notice,
//       this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of HyperDex nor the names of its contributors may be
//       used to endorse or promote products derived from this software without
//       specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

// C
#include <cassert>
#include <cstdlib>

// STL
#include <algorithm>

// HyperDex
#include "disk/cuckoo_table.h"

#define ENTRIES_PER_CACHE_LINE 5
#define CUCKOO_ROUNDS_BEFORE_FULL 16

#define MASK_UPPER_32 18446744069414584320ULL
#define MASK_LOWER_32 4294967295ULL
#define MASK_UPPER_16 4294901760ULL
#define MASK_LOWER_16 65535ULL

using hyperdex::cuckoo_returncode;
using hyperdex::cuckoo_table;

cuckoo_table :: cuckoo_table(void* table)
    : m_base(static_cast<uint32_t*>(table))
    , m_hash_table_full(false)
    , m_full_key(0)
    , m_full_val(0)
{
}

cuckoo_table :: ~cuckoo_table() throw ()
{
}

cuckoo_returncode
cuckoo_table :: insert(uint64_t key, uint64_t val)
{
    assert(!m_hash_table_full);
    uint16_t index;
    uint32_t* cache_line;
    size_t where = ENTRIES_PER_CACHE_LINE;
    size_t empty1 = ENTRIES_PER_CACHE_LINE;
    size_t empty2 = ENTRIES_PER_CACHE_LINE;

    // Look in the first table
    index = get_index1(key);
    cache_line = get_cache_line1(index);

    for (where = 0; where < ENTRIES_PER_CACHE_LINE; ++where)
    {
        if (cache_line[where * 3 + 0] == 0 &&
            cache_line[where * 3 + 1] == 0 &&
            cache_line[where * 3 + 2] == 0)
        {
            empty1 = where;
            where = ENTRIES_PER_CACHE_LINE;
            break;
        }
    }

    // Look in the second table
    index = get_index2(key);
    cache_line = get_cache_line2(index);

    for (where = 0; where < ENTRIES_PER_CACHE_LINE; ++where)
    {
        if (cache_line[where * 3 + 0] == 0 &&
            cache_line[where * 3 + 1] == 0 &&
            cache_line[where * 3 + 2] == 0)
        {
            empty2 = where;
            where = ENTRIES_PER_CACHE_LINE;
            break;
        }
    }

    uint32_t entry[3];

    // Consider empty space in either table
    if (empty1 < ENTRIES_PER_CACHE_LINE)
    {
        get_entry1(key, val, entry);
        index = get_index1(key);
        cache_line = get_cache_line1(index);
        cache_line[empty1 * 3 + 0] = entry[0];
        cache_line[empty1 * 3 + 1] = entry[1];
        cache_line[empty1 * 3 + 2] = entry[2];
        return CUCKOO_SUCCESS;
    }
    else if (empty2 < ENTRIES_PER_CACHE_LINE)
    {
        get_entry2(key, val, entry);
        index = get_index2(key);
        cache_line = get_cache_line2(index);
        cache_line[empty2 * 3 + 0] = entry[0];
        cache_line[empty2 * 3 + 1] = entry[1];
        cache_line[empty2 * 3 + 2] = entry[2];
        return CUCKOO_SUCCESS;
    }

    // Time to do the cuckoo
    unsigned table = 1;
    uint64_t cuckoo_key = key;
    uint64_t cuckoo_val = val;

    for (size_t i = 0; i < CUCKOO_ROUNDS_BEFORE_FULL; ++i)
    {
        void (cuckoo_table::*g_entry)(uint64_t key, uint64_t val, uint32_t* entry);
        uint16_t (cuckoo_table::*g_index)(uint64_t key);
        uint32_t* (cuckoo_table::*g_cache_line)(uint16_t idx);
        uint64_t (cuckoo_table::*g_key)(uint16_t idx, uint32_t* entry);

        if (table == 1)
        {
            g_entry = &cuckoo_table::get_entry1;
            g_index = &cuckoo_table::get_index1;
            g_cache_line = &cuckoo_table::get_cache_line1;
            g_key = &cuckoo_table::get_key1;
        }
        else if (table == 2)
        {
            g_entry = &cuckoo_table::get_entry2;
            g_index = &cuckoo_table::get_index2;
            g_cache_line = &cuckoo_table::get_cache_line2;
            g_key = &cuckoo_table::get_key2;
        }
        else
        {
            abort();
        }

        (this->*g_entry)(cuckoo_key, cuckoo_val, entry);
        index = (this->*g_index)(cuckoo_key);
        cache_line = (this->*g_cache_line)(index);

        for (where = 0; where < ENTRIES_PER_CACHE_LINE; ++where)
        {
            uint32_t tmp[3];
            tmp[0] = cache_line[where * 3 + 0];
            tmp[1] = cache_line[where * 3 + 1];
            tmp[2] = cache_line[where * 3 + 2];
            cache_line[where * 3 + 0] = entry[0];
            cache_line[where * 3 + 1] = entry[1];
            cache_line[where * 3 + 2] = entry[2];
            entry[0] = tmp[0];
            entry[1] = tmp[1];
            entry[2] = tmp[2];
        }

        // If we've shifted an empty entry off the end we are done
        if (entry[0] == 0 && entry[1] == 0 && entry[2] == 0)
        {
            return CUCKOO_SUCCESS;
        }

        cuckoo_key = (this->*g_key)(index, entry);
        cuckoo_val = get_val(entry);
        assert(table == 1 || table == 2);
        table = table == 1 ? 2 : 1;
    }

    // If we reach this point, we have a hash table that is full enough to
    // require that we do something to handle it
    m_hash_table_full = true;
    m_full_key = cuckoo_key;
    m_full_val = cuckoo_val;
    return CUCKOO_FULL;
}

cuckoo_returncode
cuckoo_table :: lookup(uint64_t key, std::vector<uint64_t>* vals)
{
    uint16_t index;
    uint32_t* cache_line;

    // Check the first table
    index = get_index1(key);
    cache_line = get_cache_line1(index);

    for (size_t where = 0; where < ENTRIES_PER_CACHE_LINE; ++where)
    {
        if (key == get_key1(index, cache_line + where * 3))
        {
            uint64_t val = get_val(cache_line + where * 3);

            if (val)
            {
                vals->push_back(val);
            }
        }
    }

    // Check the second table
    index = get_index2(key);
    cache_line = get_cache_line2(index);

    for (size_t where = 0; where < ENTRIES_PER_CACHE_LINE; ++where)
    {
        if (key == get_key2(index, cache_line + where * 3))
        {
            uint64_t val = get_val(cache_line + where * 3);

            if (val)
            {
                vals->push_back(val);
            }
        }
    }

    return vals->size() > 0 ? CUCKOO_SUCCESS : CUCKOO_NOT_FOUND;
}

#include <iostream>

cuckoo_returncode
cuckoo_table :: remove(uint64_t key, uint64_t val)
{
    // XXX instead of swapping with the back, we should shift all entries to a
    // lower index in the cache line.
    bool found = false;
    uint32_t entry[3];
    uint16_t index;
    uint32_t* cache_line;

    // Clean the first table
    get_entry1(key, val, entry);
    index = get_index1(key);
    cache_line = get_cache_line1(index);

    for (size_t where = 0; where < ENTRIES_PER_CACHE_LINE; ++where)
    {
        if (entry[0] == cache_line[where * 3 + 0] &&
            entry[1] == cache_line[where * 3 + 1] &&
            entry[2] == cache_line[where * 3 + 2])
        {
            cache_line[where * 3 + 0] = cache_line[(ENTRIES_PER_CACHE_LINE - 1) * 3 + 0];
            cache_line[where * 3 + 1] = cache_line[(ENTRIES_PER_CACHE_LINE - 1) * 3 + 1];
            cache_line[where * 3 + 2] = cache_line[(ENTRIES_PER_CACHE_LINE - 1) * 3 + 2];
            cache_line[(ENTRIES_PER_CACHE_LINE - 1) * 3 + 0] = 0;
            cache_line[(ENTRIES_PER_CACHE_LINE - 1) * 3 + 1] = 0;
            cache_line[(ENTRIES_PER_CACHE_LINE - 1) * 3 + 2] = 0;
            found = true;
        }
    }

    // Clean the second table
    get_entry2(key, val, entry);
    index = get_index2(key);
    cache_line = get_cache_line2(index);

    for (size_t where = 0; where < ENTRIES_PER_CACHE_LINE; ++where)
    {
        if (entry[0] == cache_line[where * 3 + 0] &&
            entry[1] == cache_line[where * 3 + 1] &&
            entry[2] == cache_line[where * 3 + 2])
        {
            cache_line[where * 3 + 0] = cache_line[(ENTRIES_PER_CACHE_LINE - 1) * 3 + 0];
            cache_line[where * 3 + 1] = cache_line[(ENTRIES_PER_CACHE_LINE - 1) * 3 + 1];
            cache_line[where * 3 + 2] = cache_line[(ENTRIES_PER_CACHE_LINE - 1) * 3 + 2];
            cache_line[(ENTRIES_PER_CACHE_LINE - 1) * 3 + 0] = 0;
            cache_line[(ENTRIES_PER_CACHE_LINE - 1) * 3 + 1] = 0;
            cache_line[(ENTRIES_PER_CACHE_LINE - 1) * 3 + 2] = 0;
            found = true;
        }
    }

    return found ? CUCKOO_SUCCESS : CUCKOO_NOT_FOUND;
}

cuckoo_returncode
cuckoo_table :: split(cuckoo_table* table1,
                      cuckoo_table* table2,
                      uint64_t* lower_bound_key,
                      uint64_t* lower_bound_val)
{
    std::vector<std::pair<uint64_t, uint64_t> > vals;

    if (m_hash_table_full)
    {
        vals.push_back(std::make_pair(m_full_key, m_full_val));
    }

    // Scan the first table to get all keys
    for (size_t i = 0; i < 65536; ++i)
    {
        uint32_t* cache_line = get_cache_line1(i);

        for (size_t where = 0; where < ENTRIES_PER_CACHE_LINE; ++where)
        {
            uint64_t k = get_key1(i, cache_line + where * 3);
            uint64_t v = get_val(cache_line + where * 3);

            if (v)
            {
                vals.push_back(std::make_pair(k, v));
            }
            else
            {
                break;
            }
        }
    }

    // Scan the second table to get all keys
    for (size_t i = 0; i < 65536; ++i)
    {
        uint32_t* cache_line = get_cache_line2(i);

        for (size_t where = 0; where < ENTRIES_PER_CACHE_LINE; ++where)
        {
            uint64_t k = get_key2(i, cache_line + where * 3);
            uint64_t v = get_val(cache_line + where * 3);

            if (v)
            {
                vals.push_back(std::make_pair(k, v));
            }
            else
            {
                break;
            }
        }
    }

    // Figure out the division point
    std::sort(vals.begin(), vals.end());
    *lower_bound_key = vals[vals.size() / 2].first;
    *lower_bound_val = vals[vals.size() / 2].second;
    cuckoo_returncode rc;

    for (size_t i = 0; i < vals.size() / 2; ++i)
    {
        rc = table1->insert(vals[i].first, vals[i].second);
        assert(rc == CUCKOO_SUCCESS);
    }

    for (size_t i = vals.size() / 2; i < vals.size(); ++i)
    {
        rc = table2->insert(vals[i].first, vals[i].second);
        assert(rc == CUCKOO_SUCCESS);
    }

    return CUCKOO_SUCCESS;
}

void
cuckoo_table :: get_entry1(uint64_t key, uint64_t val, uint32_t* entry)
{
    entry[0] = (key >> 32) & MASK_LOWER_32;
    entry[1] = (key & MASK_UPPER_16) | ((val >> 32) & MASK_LOWER_16);
    entry[2] = val & MASK_LOWER_32;
}

void
cuckoo_table :: get_entry2(uint64_t key, uint64_t val, uint32_t* entry)
{
    entry[0] = (key >> 32) & MASK_LOWER_32;
    entry[1] = ((key & MASK_LOWER_16) << 16) | ((val >> 32) & MASK_LOWER_16);
    entry[2] = val & MASK_LOWER_32;
}

uint16_t
cuckoo_table :: get_index1(uint64_t key)
{
    return key & MASK_LOWER_16;
}

uint16_t
cuckoo_table :: get_index2(uint64_t key)
{
    return (key >> 16) & MASK_LOWER_16;
}

uint32_t*
cuckoo_table :: get_cache_line1(uint16_t idx)
{
    size_t offset = idx;
    return m_base + 16 * offset;
}

uint32_t*
cuckoo_table :: get_cache_line2(uint16_t idx)
{
    size_t offset = idx;
    return m_base + 16 * (offset + 65536);
}

uint64_t
cuckoo_table :: get_key1(uint16_t idx, uint32_t* entry)
{
    uint64_t key = entry[0];
    key <<= 32;
    key |= (entry[1] & MASK_UPPER_16);
    key |= idx;
    return key;
}

uint64_t
cuckoo_table :: get_key2(uint16_t idx, uint32_t* entry)
{
    uint64_t x = idx;
    uint64_t key = entry[0];
    key <<= 32;
    key |= x << 16;
    key |= (entry[1] & MASK_UPPER_16) >> 16;
    return key;
}

uint64_t
cuckoo_table :: get_val(uint32_t* entry)
{
    uint64_t val = entry[1] & MASK_LOWER_16;
    val <<= 32;
    val |= entry[2];
    return val;
}
