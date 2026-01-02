/*
 * Copyright (c) 2012, 2014, 2017-2019, 2021 Arm Limited
 * Copyright (c) 2026 Google Inc.
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2002-2005 The Regents of The University of Michigan
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

#ifndef __BASE_ADDR_RANGE_HH__
#define __BASE_ADDR_RANGE_HH__

#include <algorithm>
#include <iterator>
#include <list>
#include <utility>
#include <vector>

#include "base/addr_range_map_policy.hh"
#include "base/bitfield.hh"
#include "base/cprintf.hh"
#include "base/logging.hh"
#include "base/types.hh"

namespace gem5
{

class AddrRange;

/**
 * Convenience typedef for a collection of address ranges
 *
 * @ingroup api_addr_range
 */
typedef std::list<AddrRange> AddrRangeList;

/**
 * The AddrRange class encapsulates an address range, and supports a
 * number of tests to check if two ranges intersect, if a range
 * contains a specific address etc. Besides a basic range, the
 * AddrRange also support interleaved ranges, to stripe across cache
 * banks, or memory controllers. The interleaving is implemented by
 * allowing a number of bits of the address, at an arbitrary bit
 * position, to be used as interleaving bits with an associated
 * matching value. In addition, to prevent uniformly strided address
 * patterns from a very biased interleaving, we also allow XOR-based
 * hashing by specifying a set of bits to XOR with before matching.
 *
 * The AddrRange is also able to coalesce a number of interleaved
 * ranges to a contiguous range.
 */
class AddrRange
{

  private:

    /// Private fields for the start and end of the range
    /// _start is the beginning of the range (inclusive).
    /// _end is not part of the range.
    Addr _start;
    Addr _end;

    std::shared_ptr<AddrMapPolicy> _policy;

  protected:
    struct Dummy {};

    // The dummy parameter Dummy distinguishes this from the other two argument
    // constructor which takes two Addrs.
    // This constructor takes multiple address ranges and merges them into a
    // single address range, if possible. If not possible, there is a fatal
    // error.
    template <class Iterator>
    AddrRange(Dummy, Iterator begin_it, Iterator end_it)
        : _start(1), _end(0), _policy(nullptr)
    {
        if (begin_it != end_it) {
            // get the values from the first one and check the others
            _start = begin_it->_start;
            _end = begin_it->_end;
            _policy = begin_it->_policy;
        }

        auto count = std::distance(begin_it, end_it);
        // either merge if got all ranges or keep this equal to the single
        // interleaved range
        if (count > 1) {
            fatal_if(!interleaved(), "Merging non-interleaved ranges?");

            std::vector<std::shared_ptr<AddrMapPolicy>> policies;
            policies.reserve(count);

            for (auto it = begin_it; it != end_it; it++) {
                fatal_if(it->_start != _start || it->_end != _end,
                         "Can only merge ranges with the same start and end");
                fatal_if(!it->_policy, "Cannot merge flat ranges");
                policies.push_back(it->_policy);
            }

            // Attempt to merge policies
            _policy = _policy->createMerged(policies);
        }
    }

  public:
    /**
     * Default constructor, creates an invalid address range.
     *
     * @ingroup api_addr_range
     */
    AddrRange() : _start(1), _end(0), _policy(nullptr) {}

    /**
     * Construct a masked interleaved address range.
     *
     * If the user provides a non empty vector of masks then the
     * address range is interleaved. Each mask determines a set of
     * bits that are xored to determine one bit of the sel value,
     * starting from the least significant bit (i.e., masks[0]
     * determines the least significant bit of sel, ...). If sel
     * matches the provided _intlv_match then the address a is in the
     * range.
     *
     * For example if the input mask is
     * _masks = { 1 << 8 | 1 << 11 | 1 << 13,
     *            1 << 15 | 1 << 17 | 1 << 19}
     *
     * Then a belongs to the address range if
     * _start <= a < _end
     * and
     * sel == _intlv_match
     * where
     * sel[0] = a[8] ^ a[11] ^ a[13]
     * sel[1] = a[15] ^ a[17] ^ a[19]
     *
     * @param _start The start address of this range
     * @param _end The end address of this range (not included in  the range)
     * @param _masks The input vector of masks
     * @param intlv_match The matching value of the xor operations
     *
     * @ingroup api_addr_range
     */
    AddrRange(Addr _start, Addr _end, const std::vector<Addr> &_masks,
              uint8_t _intlv_match)
        : _start(_start), _end(_end), _policy(nullptr)
    {
        if (!_masks.empty()) {
            // sanity checks
            fatal_if(_intlv_match >= 1ULL << _masks.size(),
                     "Match value %d does not fit in %d interleaving bits\n",
                     _intlv_match, _masks.size());
            _policy = std::make_shared<MaskedInterleavingPolicy>(_masks,
                                                                 _intlv_match);
        }
    }

    /**
     * Create an address range with modulo-based interleaving.
     *
     * If the user provides a stripe count of 1, the address range is not
     * interleaved.
     * Using modulo instead of interleaving allows for non-power
     * of 2 stripe counts.
     *
     * @param start The start address of the range.
     * @param end The end address of the range.
     * @param stripes The number of stripes (channels).
     * @param intlv_match The stripe index for this range.
     * @param intlv_low_bit The bit position of the interleaving granularity
     *                      (log2).
     *
     * @ingroup api_addr_range
     */
    AddrRange(Addr start, Addr end, uint32_t stripes, uint32_t intlv_match,
              uint32_t intlv_low_bit = 0)
        : _start(start), _end(end), _policy(nullptr)
    {
        if (stripes > 1) {
            _policy = std::make_shared<ModuloInterleavingPolicy>(
                stripes, intlv_match, intlv_low_bit);
        }
    }

    /**
     * Create an address range with sparse sub-ranges (holes).
     *
     * This constructor is useful for creating address ranges that are not
     * contiguous, such as the I/O hole in x86.
     * This constructor only enables "simple" sparse ranges, i.e. no
     * interleaving.
     *
     * @param ranges Vector of valid address chunks (start, end).
     *               Must be sorted and non-overlapping.
     *
     * @ingroup api_addr_range
     */
    AddrRange(const std::vector<std::pair<Addr, Addr>> &ranges)
        : _policy(std::make_shared<SparsePolicy>(ranges))
    {
        if (ranges.empty()) {
            _start = 1;
            _end = 0;
        } else {
            _start = ranges.front().first;
            _end = ranges.back().second;
        }
    }

    /**
     * Create an address range with sparse sub-ranges and masked interleaving.
     *
     * This constructor will create an interleaved address range in the same
     * way as the masked interleaving constructor, but with sparse sub-ranges.
     * If the masks are empty, the address range will be sparse only.
     *
     * @param ranges Vector of valid address chunks (start, end).
     *               Must be sorted and non-overlapping.
     * @param masks The input vector of masks.
     * @param intlv_match The matching value of the xor operations.
     *
     * @ingroup api_addr_range
     */
    AddrRange(const std::vector<std::pair<Addr, Addr>> &ranges,
              const std::vector<Addr> &masks, uint8_t intlv_match)
        : _policy(masks.empty()
                      ? std::make_shared<SparsePolicy>(ranges)
                      : std::make_shared<SparsePolicy>(
                            ranges, std::make_shared<MaskedInterleavingPolicy>(
                                        masks, intlv_match)))
    {
        if (ranges.empty()) {
            _start = 1;
            _end = 0;
        } else {
            _start = ranges.front().first;
            _end = ranges.back().second;
        }
    }

    /**
     * Create an address range with sparse sub-ranges and modulo interleaving.
     *
     * This constructor will create an interleaved address range in the same
     * way as the modulo interleaving constructor, but with sparse sub-ranges.
     * If the stripes are 1, the address range will be sparse only.
     *
     * @param ranges Vector of valid address chunks (start, end).
     *               Must be sorted and non-overlapping.
     * @param stripes The number of stripes (channels).
     * @param intlv_match The matching value of the modulo operation.
     * @param intlv_low_bit The lowest bit to use for the modulo operation.
     *
     * @ingroup api_addr_range
     */
    AddrRange(const std::vector<std::pair<Addr, Addr>> &ranges,
              uint32_t stripes, uint32_t intlv_match,
              uint32_t intlv_low_bit = 0)
        : _policy(stripes > 1
                      ? std::make_shared<SparsePolicy>(
                            ranges, std::make_shared<ModuloInterleavingPolicy>(
                                        stripes, intlv_match, intlv_low_bit))
                      : std::make_shared<SparsePolicy>(ranges))
    {
        if (ranges.empty()) {
            _start = 1;
            _end = 0;
        } else {
            // The policy sorts the ranges, so get the start/end from there.
            _start = subRanges().front().first;
            _end = subRanges().back().second;
        }
    }

    /**
     * Legacy constructor of AddrRange
     *
     * If the user provides a non-zero value in _intlv_high_bit the
     * address range is interleaved.
     *
     * An address a belongs to the address range if
     * _start <= a < _end
     * and
     * sel == _intlv_match
     * where
     * sel = sel1 ^ sel2
     * sel1 = a[_intlv_low_bit:_intlv_high_bit]
     * sel2 = a[_xor_low_bit:_xor_high_bit]
     * _intlv_low_bit = _intlv_high_bit - intv_bits
     * _xor_low_bit = _xor_high_bit - intv_bits
     *
     * @param _start The start address of this range
     * @param _end The end address of this range (not included in  the range)
     * @param _intlv_high_bit The MSB of the intlv bits (disabled if 0)
     * @param _xor_high_bit The MSB of the xor bit (disabled if 0)
     * @param _intlv_bits the size, in bits, of the intlv and xor bits
     * @param intlv_match The matching value of the xor operations
     *
     * @ingroup api_addr_range
     */
    AddrRange(Addr _start, Addr _end, uint8_t _intlv_high_bit,
              uint8_t _xor_high_bit, uint8_t _intlv_bits, uint8_t _intlv_match)
        : _start(_start), _end(_end), _policy(nullptr)
    {
        if (_intlv_bits) {
            fatal_if(_intlv_match >= 1ULL << _intlv_bits,
                     "Match value %d does not fit in %d interleaving bits\n",
                     _intlv_match, _intlv_bits);

            // ignore the XOR bits if not interleaving
            if (_xor_high_bit) {
                if (_xor_high_bit == _intlv_high_bit) {
                    fatal("XOR and interleave high bit must be different\n");
                } else if (_xor_high_bit > _intlv_high_bit) {
                    if ((_xor_high_bit - _intlv_high_bit) < _intlv_bits) {
                        fatal("XOR and interleave high bit must be at least "
                              "%d bits apart\n",
                              _intlv_bits);
                    }
                } else {
                    if ((_intlv_high_bit - _xor_high_bit) < _intlv_bits) {
                        fatal("Interleave and XOR high bit must be at least "
                              "%d bits apart\n",
                              _intlv_bits);
                    }
                }
            }

            std::vector<Addr> masks(_intlv_bits);
            for (auto i = 0; i < _intlv_bits; i++) {
                uint8_t bit1 = _intlv_high_bit - i;
                Addr mask = (1ULL << bit1);
                if (_xor_high_bit) {
                    uint8_t bit2 = _xor_high_bit - i;
                    mask |= (1ULL << bit2);
                }
                masks[_intlv_bits - i - 1] = mask;
            }
            _policy = std::make_shared<MaskedInterleavingPolicy>(masks,
                                                                 _intlv_match);
        }
    }

    /**
     * Create a simple address range without interleaving.
     *
     * @param _start Start address of the range.
     * @param _end End address of the range.
     *
     * @ingroup api_addr_range
     */
    AddrRange(Addr _start, Addr _end)
        : _start(_start), _end(_end), _policy(nullptr)
    {}

    /**
     * Create an address range by merging a collection of interleaved
     * ranges.
     *
     * @param ranges Interleaved ranges to be merged
     *
     * @ingroup api_addr_range
     */
    AddrRange(std::vector<AddrRange> ranges)
        : AddrRange(Dummy{}, ranges.begin(), ranges.end())
    {}
    AddrRange(std::list<AddrRange> ranges)
        : AddrRange(Dummy{}, ranges.begin(), ranges.end())
    {}

    /**
     * Determine if the range is interleaved or not.
     *
     * @return true if interleaved
     *
     * @ingroup api_addr_range
     */
    bool
    interleaved() const
    {
        return _policy != nullptr;
    }

    /**
     * Determine if this range is a sparse range.
     *
     * @return true if sparse
     *
     * @ingroup api_addr_range
     */
    bool
    isSparse() const
    {
        if (_policy) {
            return dynamic_cast<SparsePolicy *>(_policy.get()) != nullptr;
        }
        return false;
    }

    std::vector<std::pair<Addr, Addr>>
    subRanges() const
    {
        if (_policy) {
            return dynamic_cast<SparsePolicy *>(_policy.get())->getSubRanges();
        }
        return std::vector<std::pair<Addr, Addr>>{{_start, _end}};
    }

    /**
     * Determing the interleaving granularity of the range.
     *
     * @return The size of the regions created by the interleaving bits
     *
     * @ingroup api_addr_range
     */
    uint64_t
    granularity() const
    {
        if (_policy) {
            return _policy->granularity();
        } else {
            return size();
        }
    }

    /**
     * Determine the number of interleaved address stripes this range
     * is part of.
     *
     * @return The number of stripes spanned by the interleaving bits
     *
     * @ingroup api_addr_range
     */
    uint32_t
    stripes() const
    {
        if (_policy) {
            return _policy->stripes();
        }
        return 1;
    }

    /**
     * Get the size of the address range. For a case where
     * interleaving is used we make the simplifying assumption that
     * the size is a divisible by the size of the interleaving slice.
     *
     * @ingroup api_addr_range
     */
    Addr
    size() const
    {
        if (_policy) {
            return _policy->size(_start, _end);
        }
        return (_end - _start);
    }

    /**
     * Determine if the range is valid.
     *
     * @ingroup api_addr_range
     */
    bool valid() const { return _start <= _end; }

    /**
     * Get the start address of the range.
     *
     * @ingroup api_addr_range
     */
    Addr start() const { return _start; }

    /**
     * Get the end address of the range.
     *
     * @ingroup api_addr_range
     */
    Addr end() const { return _end; }

    /**
     * Get a string representation of the range. This could
     * alternatively be implemented as a operator<<, but at the moment
     * that seems like overkill.
     *
     * @ingroup api_addr_range
     */
    std::string
    to_string() const
    {
        if (_policy) {
            return _policy->to_string(_start, _end);
        }
        return csprintf("[%#llx:%#llx]", _start, _end);
    }

    /**
     * Determine if another range merges with the current one, i.e. if
     * they are part of the same contigous range and have the same
     * interleaving bits.
     *
     * @param r Range to evaluate merging with
     * @return true if the two ranges would merge
     *
     * @ingroup api_addr_range
     */
    bool
    mergesWith(const AddrRange& r) const
    {
        bool same_policy = false;
        if (!_policy && !r._policy) {
            same_policy = true;
        } else if (_policy && r._policy) {
            same_policy = _policy->canMerge(r._policy);
        }

        return r._start == _start && r._end == _end && same_policy;
    }

    /**
     * Determine if another range intersects this one, i.e. if there
     * is an address that is both in this range and the other
     * range. No check is made to ensure either range is valid.
     *
     * @param r Range to intersect with
     * @return true if the intersection of the two ranges is not empty
     *
     * @ingroup api_addr_range
     */
    bool
    intersects(const AddrRange& r) const
    {
        if (_start >= r._end || _end <= r._start) {
            return false;
        }

        if (_policy && !_policy->checkIntersection(_start, _end, r._start,
                                                   r._end, r._policy)) {
            return false;
        }

        if (r._policy && !r._policy->checkIntersection(
                             r._start, r._end, _start, _end, _policy)) {
            return false;
        }

        return true;
    }

    /**
     * Determine if this range is a subset of another range, i.e. if
     * every address in this range is also in the other range. No
     * check is made to ensure either range is valid.
     *
     * @param r Range to compare with
     * @return true if the this range is a subset of the other one
     *
     * @ingroup api_addr_range
     */
    bool
    isSubset(const AddrRange& r) const
    {
        if (interleaved())
            panic("Cannot test subset of interleaved range %s\n", to_string());

        // This address range is not interleaved and therefore it
        // suffices to check the upper bound, the lower bound and
        // whether it would fit in a continuous segment of the input
        // addr range.
        if (r.interleaved()) {
          if (r.contains(_start) && size() <= r.granularity()) {
            // simplify checking for 1 element ranges
            // no need to re-check r.contains(_end -1) if
            // it is the same as _start
            if (_start == _end - 1) {
              return true;
            }
            //otherwise check if it also contains the end.
            return r.contains(_end -1);
          }
          return false;
        } else {

            if (_end <= _start){
                // Special case: if our range wraps around that is
                // _end is 2^64 so it wraps to 0.
                // In this case we will be a subset only if r._end
                // also wraps around.
                return _start >= r._start && r._end == 0;
            } else if (r._end <= r._start){
                // Special case: if r wraps around that is
                // r._end is 2^64 so it wraps to 0.
                // In this case we will be a subset only if our _start
                // is within r._start/ _end does not matter
                // because r wraps around.
                return _start >= r._start;
            } else {
                // Normal case: Check if our range is completely within 'r'.
                return _start >= r._start && _end <= r._end;
            }

        }
    }

    /**
     * Determine if the range contains an address.
     *
     * @param a Address to compare with
     * @return true if the address is in the range
     *
     * @ingroup api_addr_range
     */
    bool
    contains(const Addr& a) const
    {
        // check if the address is in the range and if there is either
        // no interleaving, or with interleaving also if the selected
        // bits from the address match the interleaving value
        bool in_range = a >= _start && a < _end;
        if (in_range) {
            if (_policy) {
                return _policy->contains(_start, _end, a);
            }
            return true;
        }
        return false;
    }

    /**
     * Remove the interleaving bits from an input address.
     *
     * This function returns a new address in a continous range [
     * start, start + size / intlv_bits). We can achieve this by
     * discarding the LSB in each mask.
     *
     * @param a the input address
     * @return the new address, or the input address if not interleaved
     *
     * @ingroup api_addr_range
     */
    inline Addr
    removeIntlvBits(Addr a) const
    {
        if (auto p =
                std::dynamic_pointer_cast<MaskedInterleavingPolicy>(_policy)) {
            return p->removeIntlvBits(a);
        }
        return a;
    }

    /**
     * This method adds the interleaving bits removed by
     * removeIntlvBits.
     *
     * @ingroup api_addr_range
     */
    inline Addr
    addIntlvBits(Addr a) const
    {
        if (auto p =
                std::dynamic_pointer_cast<MaskedInterleavingPolicy>(_policy)) {
            return p->addIntlvBits(a);
        }
        return a;
    }

    /**
     * Determine the offset of an address within the range.
     *
     * This function returns the offset of the given address from the
     * starting address discarding any bits that are used for
     * interleaving. This way we can convert the input address to a
     * new unique address in a continuous range that starts from 0.
     *
     * @param the input address
     * @return the flat offset in the address range
     *
     * @ingroup api_addr_range
     */
    Addr
    getOffset(const Addr& a) const
    {
        bool in_range = a >= _start && a < _end;
        if (!in_range) {
            return MaxAddr;
        }
        if (_policy) {
            return _policy->getOffset(_start, _end, a);
        } else {
            return a - _start;
        }
    }

    /**
     * Subtract a list of intervals from the range and return
     * the resulting collection of ranges, so that the union
     * of the two lists cover the original range
     *
     * The exclusion list can contain overlapping ranges
     * Interleaving ranges are not supported and will fail the
     * assertion.
     *
     * @param the input exclusion list
     * @return the resulting collection of ranges
     *
     * @ingroup api_addr_range
     */
    AddrRangeList
    exclude(const AddrRangeList &exclude_ranges) const
    {
        assert(!interleaved());

        auto sorted_ranges = exclude_ranges;
        sorted_ranges.sort();

        std::list<AddrRange> ranges;

        Addr next_start = start();
        for (const auto &e : sorted_ranges) {
            assert(!e.interleaved());
            if (!intersects(e)) {
                continue;
            }

            if (e.start() <= next_start) {
                if (e.end() < end()) {
                    if (next_start < e.end()) {
                        next_start = e.end();
                    }
                } else {
                    return ranges;
                }
            } else {
                ranges.push_back(AddrRange(next_start, e.start()));
                if (e.end() < end()) {
                    next_start = e.end();
                } else {
                    return ranges;
                }
            }
        }

        if (next_start < end()) {
            ranges.push_back(AddrRange(next_start, end()));
        }

        return ranges;
    }

    AddrRangeList
    exclude(const AddrRange &excluded_range) const
    {
        return exclude(AddrRangeList{excluded_range});
    }

    /**
     * Less-than operator used to turn an STL map into a binary search
     * tree of non-overlapping address ranges.
     *
     * @param r Range to compare with
     * @return true if the start address is less than that of the other range
     *
     * @ingroup api_addr_range
     */
    bool
    operator<(const AddrRange& r) const
    {
        if (_start != r._start) {
            return _start < r._start;
        } else {
            // For now assume that the end is also the same.
            // If both regions are interleaved, ask policy to compare.
            if (interleaved() && r.interleaved()) {
                return _policy->lessThan(r._policy);
            } else {
                return interleaved();
            }
        }
    }

    /**
     * @ingroup api_addr_range
     */
    bool
    operator==(const AddrRange& r) const
    {
        if (_start != r._start)    return false;
        if (_end != r._end)      return false;
        if (!_policy && !r._policy) {
            return true;
        }
        if (_policy && r._policy) {
            return _policy->isEquivalent(r._policy);
        }
        return false;
    }

    /**
     * @ingroup api_addr_range
     */
    bool
    operator!=(const AddrRange& r) const
    {
        return !(*this == r);
    }

    /**
     * @ingroup api_addr_range
     */
    AddrRange
    operator&(const AddrRange& r) const
    {
        panic_if(this->interleaved() || r.interleaved(),
                 "Cannot calculate intersection of interleaved ranges.");
        Addr start = std::max(this->_start, r._start);
        Addr end = std::min(this->_end, r._end);
        if (end <= start) {
            return AddrRange(0, 0);
        }
        return AddrRange(start, end);
    }
};

static inline AddrRangeList
operator-(const AddrRange &range, const AddrRangeList &to_exclude)
{
    return range.exclude(to_exclude);
}

static inline AddrRangeList
operator-(const AddrRange &range, const AddrRange &to_exclude)
{
    return range.exclude(to_exclude);
}

static inline AddrRangeList
exclude(const AddrRangeList &base, AddrRangeList to_exclude)
{
    to_exclude.sort();

    AddrRangeList ret;
    for (const auto &range: base)
        ret.splice(ret.end(), range.exclude(to_exclude));

    return ret;
}

static inline AddrRangeList
exclude(const AddrRangeList &base, const AddrRange &to_exclude)
{
    return exclude(base, AddrRangeList{to_exclude});
}

static inline AddrRangeList
operator-(const AddrRangeList &base, const AddrRangeList &to_exclude)
{
    return exclude(base, to_exclude);
}

static inline AddrRangeList
operator-(const AddrRangeList &base, const AddrRange &to_exclude)
{
    return exclude(base, to_exclude);
}

static inline AddrRangeList &
operator-=(AddrRangeList &base, const AddrRangeList &to_exclude)
{
    base = exclude(base, to_exclude);
    return base;
}

static inline AddrRangeList &
operator-=(AddrRangeList &base, const AddrRange &to_exclude)
{
    base = exclude(base, to_exclude);
    return base;
}

/**
 * Factory method to create an address range from a start address and
 * a size.
 *
 * @param start The start address of this range
 * @param size The size of the range
 *
 * @ingroup api_addr_range
 */
static inline AddrRange
RangeSize(Addr start, Addr size)
{
    return AddrRange(start, start + size);
}

/**
 * Factory method to create an address range from a start address and
 * an end address.
 *
 * @param start The start address of this range
 * @param end The end address of this range
 *
 * @ingroup api_addr_range
 */
static inline AddrRange
RangeEx(Addr start, Addr end)
{
    return AddrRange(start, end);
}

/**
 * Factory method to create an address range from a start address and
 * an end address (inclusive).
 *
 * @param start The start address of this range
 * @param end The end address of this range
 *
 * @ingroup api_addr_range
 */
static inline AddrRange
RangeIn(Addr start, Addr end)
{
    return AddrRange(start, end + 1);
}

} // namespace gem5

#endif // __BASE_ADDR_RANGE_HH__
