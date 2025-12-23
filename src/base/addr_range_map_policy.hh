/**
 * Copyright (c) 2023 The Regents of The University of California
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __BASE_ADDR_RANGE_MAP_POLICY_HH__
#define __BASE_ADDR_RANGE_MAP_POLICY_HH__

#include <algorithm>
#include <memory>
#include <vector>

#include "base/bitfield.hh"
#include "base/cprintf.hh"
#include "base/logging.hh"
#include "base/types.hh"

namespace gem5
{

class AddrMapPolicy
{
  public:
    virtual ~AddrMapPolicy() = default;

    /**
     * Checks if the address a is contained within the range defined by
     * rangeStart and rangeEnd, according to this policy.
     * Note: The basic bounds check (a >= start && a < end) should happen
     * BEFORE calling this method.
     */
    virtual bool contains(Addr rangeStart, Addr rangeEnd, Addr a) const = 0;

    /**
     * Calculates the offset of address a within the range.
     */
    virtual Addr getOffset(Addr rangeStart, Addr rangeEnd, Addr a) const = 0;

    /**
     * Inverse of getOffset.
     */
    virtual Addr toInt(Addr rangeStart, Addr rangeEnd, Addr offset) const = 0;

    /**
     * Determing the interleaving granularity of the range.
     */
    virtual uint64_t granularity() const = 0;

    /**
     * Determine the number of interleaved address stripes this range
     * is part of.
     */
    virtual uint32_t stripes() const = 0;

    /**
     * Get the size of the address range.
     */
    virtual Addr size(Addr rangeStart, Addr rangeEnd) const = 0;

    /**
     * Returns true if this policy is "equivalent" to another.
     * Used for equality operators.
     */
    virtual bool
    isEquivalent(const std::shared_ptr<AddrMapPolicy> &other) const = 0;

    /**
     * Returns true if this policy is compatible with another for merging.
     * E.g. same interleaving geometry (masks) but potentially different match
     * values.
     */
    virtual bool
    canMerge(const std::shared_ptr<AddrMapPolicy> &other) const = 0;
};

class MaskedInterleavingPolicy : public AddrMapPolicy
{
  private:
    std::vector<Addr> masks;
    uint8_t intlvMatch;

  public:
    MaskedInterleavingPolicy(const std::vector<Addr> &_masks,
                             uint8_t _intlvMatch)
        : masks(_masks), intlvMatch(_intlvMatch)
    {}

    bool
    contains(Addr rangeStart, Addr rangeEnd, Addr a) const override
    {
        auto sel = 0;
        for (unsigned int i = 0; i < masks.size(); i++) {
            Addr masked = a & masks[i];
            sel |= (popCount(masked) % 2) << i;
        }
        return sel == intlvMatch;
    }

    Addr
    getOffset(Addr rangeStart, Addr rangeEnd, Addr a) const override
    {
        return removeIntlvBits(a) - removeIntlvBits(rangeStart);
    }

    Addr
    toInt(Addr rangeStart, Addr rangeEnd, Addr offset) const override
    {
        return addIntlvBits(removeIntlvBits(rangeStart) + offset);
    }

    uint64_t
    granularity() const override
    {
        auto combined_mask = 0;
        for (auto mask : masks) {
            combined_mask |= mask;
        }
        const uint8_t lowest_bit = ctz64(combined_mask);
        return 1ULL << lowest_bit;
    }

    uint32_t
    stripes() const override
    {
        return 1ULL << masks.size();
    }

    Addr
    size(Addr rangeStart, Addr rangeEnd) const override
    {
        return (rangeEnd - rangeStart) >> masks.size();
    }

    bool
    isEquivalent(const std::shared_ptr<AddrMapPolicy> &other) const override
    {
        auto casted =
            std::dynamic_pointer_cast<MaskedInterleavingPolicy>(other);
        if (!casted) {
            return false;
        }
        return masks == casted->masks && intlvMatch == casted->intlvMatch;
    }

    bool
    canMerge(const std::shared_ptr<AddrMapPolicy> &other) const override
    {
        auto casted =
            std::dynamic_pointer_cast<MaskedInterleavingPolicy>(other);
        if (!casted) {
            return false;
        }
        return masks == casted->masks;
    }

    // Helper functions from original AddrRange
    inline Addr
    removeIntlvBits(Addr a) const
    {
        auto masks_lsb = std::make_unique<int[]>(masks.size());
        for (unsigned int i = 0; i < masks.size(); i++) {
            masks_lsb[i] = ctz64(masks[i]);
        }
        std::sort(masks_lsb.get(), masks_lsb.get() + masks.size());

        for (unsigned int i = 0; i < masks.size(); i++) {
            const int intlv_bit = masks_lsb[i];
            if (intlv_bit > 0) {
                a = insertBits(a >> 1, intlv_bit - i - 1, 0, a);
            } else {
                a >>= 1;
            }
        }
        return a;
    }

    inline Addr
    addIntlvBits(Addr a) const
    {
        auto masks_lsb = std::make_unique<int[]>(masks.size());
        for (unsigned int i = 0; i < masks.size(); i++) {
            masks_lsb[i] = ctz64(masks[i]);
        }
        std::sort(masks_lsb.get(), masks_lsb.get() + masks.size());

        for (unsigned int i = 0; i < masks.size(); i++) {
            const int intlv_bit = masks_lsb[i];
            if (intlv_bit > 0) {
                a = insertBits(a << 1, intlv_bit - 1, 0, a);
            } else {
                a <<= 1;
            }
        }

        for (unsigned int i = 0; i < masks.size(); i++) {
            const int lsb = ctz64(masks[i]);
            const Addr intlv_bit = bits(intlvMatch, i);
            const Addr masked = a & masks[i] & ~(1 << lsb);
            a = insertBits(a, lsb, intlv_bit ^ popCount(masked));
        }
        return a;
    }

    const std::vector<Addr> &
    getMasks() const
    {
        return masks;
    }
    uint8_t
    getMatch() const
    {
        return intlvMatch;
    }
};

} // namespace gem5

#endif // __BASE_ADDR_RANGE_MAP_POLICY_HH__
