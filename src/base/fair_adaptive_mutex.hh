/*
 * Copyright (c) 2026 Google LLC
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

#ifndef __BASE_FAIR_ADAPTIVE_MUTEX_HH__
#define __BASE_FAIR_ADAPTIVE_MUTEX_HH__

#include <atomic>
#include <chrono>
#include <cstdint>

#include "base/compiler.hh"

namespace gem5
{

/**
 * A fair (FIFO) mutex whose waiters spin briefly before they sleep.
 *
 * Where it is used. The two locks of an event queue: the service lock
 * (EventQueue::service_mutex), taken by the queue's own thread while it
 * services events and by other threads when they schedule an event onto
 * that queue (EventQueue::ScopedMigration), and the lock on the queue of
 * asynchronously inserted events (EventQueue::async_queue_mutex). In a
 * single-threaded simulation neither is ever contended, so the fast path
 * is what matters: an atomic ticket, no kernel call. When somebody does
 * have to wait, the ticket also makes the lock fair (FIFO).
 *
 * Why a spin phase. In a multi-event-queue simulation of a tiled design
 * most messages travel between tiles that live on different queues, and
 * every such message hands the receiving queue's lock from one thread
 * to another. The critical sections are short (one enqueue), but a
 * waiter that sleeps in the kernel pays a wake-up of roughly ten
 * microseconds, longer than a whole simulation quantum. The usual remedy
 * is an adaptive mutex: spin for a bounded time first, sleep only if the
 * holder does not release the lock in that time. That is what this class
 * adds to the fair ticket lock the event queues used before
 * (FairUncontendedMutex, which it replaces); the fairness and the
 * uncontended fast path are unchanged.
 *
 * The spin budget is 100 microseconds (matching Barrier::defaultSpinBudget),
 * checked every 64 pause iterations. Because the holder's critical
 * section is one enqueue, when every simulation thread has a host core
 * the lock is almost always handed over within the spin; when the
 * holder's thread has lost its CPU, the waiter parks via
 * std::atomic::wait on head_id and unlock() notifies only when at least
 * one waiter is parked.
 */
class FairAdaptiveMutex
{
  public:
    /** Spin budget before a waiter parks (100 microseconds). */
    static constexpr std::chrono::nanoseconds defaultSpinBudget{100000};

    /** Mutex with the default spin budget. */
    FairAdaptiveMutex() : FairAdaptiveMutex(defaultSpinBudget) {}

    /** Mutex with an explicit spin budget (zero parks at once). */
    explicit FairAdaptiveMutex(std::chrono::nanoseconds spin_budget)
        : spinBudget(spin_budget)
    {}

    void
    lock()
    {
        const uint32_t slot_id =
            tail_id.fetch_add(1, std::memory_order_acq_rel);
        if (slot_id == head_id.load(std::memory_order_acquire)) {
            return;
        }
        // Spin for at most spinBudget, then park until our ticket reaches
        // head_id. Sample steady_clock::now() only every 64 cpuRelax()
        // iterations (and defer initializing deadline until iter == 64) so
        // short contentions that resolve within the first 64 iterations avoid
        // paying clock::now() overhead on the fast path.
        if (spinBudget.count() <= 0) {
            park(slot_id);
            return;
        }
        using clock = std::chrono::steady_clock;
        clock::time_point deadline{};
        uint32_t iter = 0;
        while (head_id.load(std::memory_order_acquire) != slot_id) {
            if ((++iter & 63) != 0) {
                cpuRelax();
                continue;
            }
            if (iter == 64) {
                deadline = clock::now() + spinBudget;
            } else if (clock::now() >= deadline) {
                park(slot_id);
                return;
            }
            cpuRelax();
        }
    }

    void
    unlock()
    {
        // Advance the ticket head, then wake parked waiters if any.
        // The seq_cst increment of head_id and seq_cst load of parked
        // pair with park()'s seq_cst increment of parked and check of
        // head_id, so a waiter that decides to sleep is either seen
        // here or sees the updated head_id before it sleeps.
        head_id.fetch_add(1, std::memory_order_seq_cst);
        if (parked.load(std::memory_order_seq_cst) > 0) {
            head_id.notify_all();
        }
    }

  private:
    /**
     * Sleep until `head_id` reaches `slot_id`. The seq_cst increment of
     * `parked` before loading `head_id` pairs with `unlock()`'s seq_cst
     * increment of `head_id` and load of `parked`.
     */
    void
    park(uint32_t slot_id)
    {
        parked.fetch_add(1, std::memory_order_seq_cst);
        uint32_t cur = head_id.load(std::memory_order_seq_cst);
        while (cur != slot_id) {
            head_id.wait(cur, std::memory_order_seq_cst);
            cur = head_id.load(std::memory_order_seq_cst);
        }
        parked.fetch_sub(1, std::memory_order_seq_cst);
    }

    const std::chrono::nanoseconds spinBudget;
    /** Ticket of the current lock holder; waiters spin on it. */
    alignas(64) std::atomic<uint32_t> head_id{0};
    /** Next ticket to hand out in lock() (own cache line). */
    alignas(64) std::atomic<uint32_t> tail_id{0};
    /** Waiters sleeping in park(); unlock() notifies only if > 0. */
    alignas(64) std::atomic<uint32_t> parked{0};
};

} // namespace gem5

#endif // __BASE_FAIR_ADAPTIVE_MUTEX_HH__
