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
#include <typeinfo>
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
     * Determine if another range merges with the current one, i.e. if
     * they are part of the same contigous range and have the same
     * interleaving bits.
     */
    virtual bool
    canMerge(const std::shared_ptr<AddrMapPolicy> &other) const = 0;

    /**
     * Compare policies for sorting.
     */
    virtual bool
    lessThan(const std::shared_ptr<AddrMapPolicy> &other) const
    {
        auto ptr = other.get();
        if (typeid(*this) != typeid(*ptr)) {
            return typeid(*this).before(typeid(*ptr));
        }
        return false;
    }

    /**
     * Check logic intersection with another range.
     */
    virtual bool
    checkIntersection(Addr myStart, Addr myEnd, Addr otherStart, Addr otherEnd,
                      const std::shared_ptr<AddrMapPolicy> &otherPolicy) const
    {
        return true;
    }

    virtual std::string
    to_string(Addr start, Addr end) const
    {
        return csprintf("[%#llx:%#llx]", start, end);
    }
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

    std::string
    to_string(Addr start, Addr end) const override
    {
        std::string str;
        for (unsigned int i = 0; i < masks.size(); i++) {
            str += " ";
            Addr mask = masks[i];
            while (mask) {
                auto bit = ctz64(mask);
                mask &= ~(1ULL << bit);
                str += csprintf("a[%d]^", bit);
            }
            str += csprintf("\b=%d", bits(intlvMatch, i));
        }
        return csprintf("[%#llx:%#llx]%s", start, end, str);
    }

    bool
    lessThan(const std::shared_ptr<AddrMapPolicy> &other) const override
    {
        auto ptr = other.get();
        if (typeid(*this) != typeid(*ptr)) {
            return AddrMapPolicy::lessThan(other);
        }
        auto casted =
            std::static_pointer_cast<MaskedInterleavingPolicy>(other);
        if (masks != casted->masks) {
            return masks < casted->masks;
        }
        return intlvMatch < casted->intlvMatch;
    }

    bool
    checkIntersection(
        Addr myStart, Addr myEnd, Addr otherStart, Addr otherEnd,
        const std::shared_ptr<AddrMapPolicy> &otherPolicy) const override
    {
        auto casted =
            std::dynamic_pointer_cast<MaskedInterleavingPolicy>(otherPolicy);
        if (casted) {
            if (masks == casted->masks) {
                return intlvMatch == casted->intlvMatch;
            }
        }
        return true;
    }
};

/**
 * This policy implements modulo-based interleaving, e.g. for systems
 * with a non-power-of-two number of channels.
 *
 * It assumes a simple interleaving scheme:
 * addr % stripes == match
 *
 * However, since interleaving is usually done at a cache line (or larger)
 * granularity, the check is effectively:
 * (addr >> intlvBit) % stripes == match
 */
class ModuloInterleavingPolicy : public AddrMapPolicy
{
  private:
    const uint32_t nStripes;
    const uint32_t intlvMatch;
    const uint32_t intlvLowBit;

  public:
    ModuloInterleavingPolicy(uint32_t stripes, uint32_t match, uint32_t bit)
        : nStripes(stripes), intlvMatch(match), intlvLowBit(bit)
    {}

    bool
    contains(Addr rangeStart, Addr rangeEnd, Addr a) const override
    {
        if (a < rangeStart || a >= rangeEnd) {
            return false;
        }
        return ((a >> intlvLowBit) % nStripes) == intlvMatch;
    }

    uint32_t
    getStripes() const
    {
        return nStripes;
    }
    uint32_t
    getMatch() const
    {
        return intlvMatch;
    }
    uint32_t
    getLowBit() const
    {
        return intlvLowBit;
    }

    Addr
    getOffset(Addr rangeStart, Addr rangeEnd, Addr a) const override
    {
        return toCompact(a) - toCompact(rangeStart);
    }

    Addr
    toInt(Addr rangeStart, Addr rangeEnd, Addr offset) const override
    {
        return fromCompact(offset + toCompact(rangeStart));
    }

    uint64_t
    granularity() const override
    {
        return 1ULL << intlvLowBit;
    }

    uint32_t
    stripes() const override
    {
        return nStripes;
    }

    Addr
    size(Addr rangeStart, Addr rangeEnd) const override
    {
        return toCompact(rangeEnd) - toCompact(rangeStart);
    }

    bool
    isEquivalent(const std::shared_ptr<AddrMapPolicy> &other) const override
    {
        auto casted =
            std::dynamic_pointer_cast<ModuloInterleavingPolicy>(other);
        if (!casted) {
            return false;
        }
        return nStripes == casted->nStripes &&
               intlvMatch == casted->intlvMatch &&
               intlvLowBit == casted->intlvLowBit;
    }

    bool
    canMerge(const std::shared_ptr<AddrMapPolicy> &other) const override
    {
        auto casted =
            std::dynamic_pointer_cast<ModuloInterleavingPolicy>(other);
        if (!casted) {
            return false;
        }
        return nStripes == casted->nStripes &&
               intlvLowBit == casted->intlvLowBit;
    }

    bool
    lessThan(const std::shared_ptr<AddrMapPolicy> &other) const override
    {
        auto ptr = other.get();
        if (typeid(*this) != typeid(*ptr)) {
            return AddrMapPolicy::lessThan(other);
        }
        auto casted =
            std::static_pointer_cast<ModuloInterleavingPolicy>(other);
        if (nStripes != casted->nStripes) {
            return nStripes < casted->nStripes;
        }
        if (intlvLowBit != casted->intlvLowBit) {
            return intlvLowBit < casted->intlvLowBit;
        }
        return intlvMatch < casted->intlvMatch;
    }

    bool
    checkIntersection(
        Addr myStart, Addr myEnd, Addr otherStart, Addr otherEnd,
        const std::shared_ptr<AddrMapPolicy> &otherPolicy) const override
    {
        auto casted =
            std::dynamic_pointer_cast<ModuloInterleavingPolicy>(otherPolicy);
        if (casted) {
            if (nStripes == casted->nStripes &&
                intlvLowBit == casted->intlvLowBit) {
                return intlvMatch == casted->intlvMatch;
            }
        }
        return true;
    }

  private:
    Addr
    toCompact(Addr a) const
    {
        return ((a >> intlvLowBit) / nStripes) << intlvLowBit |
               (a & ((1ULL << intlvLowBit) - 1));
    }

    Addr
    fromCompact(Addr a) const
    {
        return (((a >> intlvLowBit) * nStripes + intlvMatch) << intlvLowBit) |
               (a & ((1ULL << intlvLowBit) - 1));
    }
};

/**
 * This policy implements sparse address ranges, where specific
 * subsets of the range are valid address holes are skipped.
 *
 * This is useful for modeling fragmented memory maps or systems
 * where a single logical range covers multiple discontiguous
 * physical ranges.
 */
class SparsePolicy : public AddrMapPolicy
{
  private:
    std::vector<std::pair<Addr, Addr>> subRanges;

  public:
    SparsePolicy(const std::vector<std::pair<Addr, Addr>> &ranges)
        : subRanges(ranges)
    {
        // TODO: Sort and merge ranges if needed?
        // For now assume user provides sorted, non-overlapping ranges.
    }

    bool
    contains(Addr rangeStart, Addr rangeEnd, Addr a) const override
    {
        // Simple linear search or binary search
        for (const auto &r : subRanges) {
            if (a >= r.first && a < r.second) {
                return true;
            }
        }
        return false;
    }

    Addr
    getOffset(Addr rangeStart, Addr rangeEnd, Addr a) const override
    {
        Addr offset = 0;
        for (const auto &r : subRanges) {
            if (a >= r.first && a < r.second) {
                return offset + (a - r.first);
            }
            offset += (r.second - r.first);
        }
        // Should not reach here if a is in range
        return offset;
    }

    Addr
    toInt(Addr rangeStart, Addr rangeEnd, Addr offset) const override
    {
        Addr currentOffset = 0;
        for (const auto &r : subRanges) {
            Addr size = r.second - r.first;
            if (offset < currentOffset + size) {
                return r.first + (offset - currentOffset);
            }
            currentOffset += size;
        }
        // exceed range
        return subRanges.back().second; // or some error?
    }

    uint64_t
    granularity() const override
    {
        return 0; // Not interleaved
    }

    uint32_t
    stripes() const override
    {
        return 1; // Not interleaved
    }

    Addr
    size(Addr rangeStart, Addr rangeEnd) const override
    {
        Addr total = 0;
        for (const auto &r : subRanges) {
            total += (r.second - r.first);
        }
        return total;
    }

    bool
    isEquivalent(const std::shared_ptr<AddrMapPolicy> &other) const override
    {
        auto casted = std::dynamic_pointer_cast<SparsePolicy>(other);
        if (!casted) {
            return false;
        }
        return subRanges == casted->subRanges;
    }

    bool
    canMerge(const std::shared_ptr<AddrMapPolicy> &other) const override
    {
        // Sparse ranges don't merge in the traditional interleaving sense
        return false;
    }

    std::string
    to_string(Addr start, Addr end) const override
    {
        std::string s = csprintf("Sparse[%#llx:%#llx]", start, end);
        for (const auto &r : subRanges) {
            s += csprintf(":[%#llx:%#llx]", r.first, r.second);
        }
        return s;
    }

    bool
    lessThan(const std::shared_ptr<AddrMapPolicy> &other) const override
    {
        auto ptr = other.get();
        if (typeid(*this) != typeid(*ptr)) {
            return AddrMapPolicy::lessThan(other);
        }
        auto casted = std::static_pointer_cast<SparsePolicy>(other);
        return subRanges < casted->subRanges;
    }

    bool
    checkIntersection(
        Addr myStart, Addr myEnd, Addr otherStart, Addr otherEnd,
        const std::shared_ptr<AddrMapPolicy> &otherPolicy) const override
    {
        for (const auto &r : subRanges) {
            Addr overlapStart = std::max(r.first, otherStart);
            Addr overlapEnd = std::min(r.second, otherEnd);
            if (overlapStart < overlapEnd) {
                // If there is a policy, ask it if it matches this flat chunk
                if (!otherPolicy ||
                    otherPolicy->checkIntersection(
                        otherStart, otherEnd, r.first, r.second, nullptr)) {
                    return true;
                }
            }
        }
        return false;
    }

    const std::vector<std::pair<Addr, Addr>> &
    getSubRanges() const
    {
        return subRanges;
    }
};

} // namespace gem5

#endif // __BASE_ADDR_RANGE_MAP_POLICY_HH__
