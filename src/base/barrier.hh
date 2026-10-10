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

#ifndef __BASE_BARRIER_HH__
#define __BASE_BARRIER_HH__

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "base/compiler.hh"

namespace gem5
{

/**
 * A reusable thread barrier for the per-quantum synchronisation of a
 * multi-event-queue simulation.
 *
 * Why not std::barrier. Every simulation thread enters a global barrier
 * at the end of every simulation quantum, and a quantum can be as short
 * as one core cycle. std::barrier parks waiting threads in the kernel,
 * so releasing N threads costs N kernel wake-ups of roughly ten
 * microseconds each; with a short quantum the threads spend more time
 * falling asleep and waking up than simulating.
 *
 * What this barrier does. A waiter first spins on a generation counter
 * (with a CPU pause hint) and parks in the kernel only if the other
 * threads have not arrived within a fixed budget of 100 microseconds.
 * Parking uses std::atomic::wait on the generation counter and the
 * releaser notifies only when at least one waiter is parked, so the
 * common fast path is a store and a load. A quantum's work is
 * microseconds, so a waiter reaches the budget only when the thread it
 * waits for has lost its CPU; it then sleeps instead of competing with
 * that thread. There is no parameter: a process cannot tell from inside
 * a shared machine or container how many CPUs it will really be given,
 * so the budget cannot usefully be chosen from the host, and 100
 * microseconds bounds spin waste under CPU contention while still
 * covering intra-quantum skew when CPUs are available.
 *
 * Semantics. The interface mirrors the subset of std::barrier used by
 * gem5: `expected` threads call arrive_and_wait(); the completion
 * function runs on the last arriving thread before any thread is
 * released; the barrier is reusable.
 */
template <class CompletionFunction> class Barrier
{
  public:
    /** Spin budget before a waiter parks (100 microseconds). */
    static constexpr std::chrono::nanoseconds defaultSpinBudget{100000};

    /** Barrier for `expected` threads with the default spin budget. */
    Barrier(std::ptrdiff_t expected, CompletionFunction completion)
        : Barrier(expected, std::move(completion), defaultSpinBudget)
    {}

    /** Barrier with an explicit spin budget (zero parks at once). */
    Barrier(std::ptrdiff_t expected, CompletionFunction completion,
            std::chrono::nanoseconds spin_budget)
        : expected(expected),
          spinBudget(spin_budget),
          completion(std::move(completion))
    {}

    void
    arrive_and_wait()
    {
        const uint32_t gen = generation.load(std::memory_order_acquire);
        if (arrived.fetch_add(1, std::memory_order_acq_rel) + 1 == expected) {
            // Last arrival: run the completion step, then open the
            // barrier for this generation. `arrived` is reset before the
            // release so early arrivals of the next generation count
            // correctly.
            arrived.store(0, std::memory_order_relaxed);
            completion();
            // The seq_cst store and the seq_cst load of `parked` pair
            // with park()'s seq_cst increment and check, so a waiter that
            // decides to sleep is either seen here or sees the new
            // generation before it sleeps: no lost wake-up.
            generation.store(gen + 1, std::memory_order_seq_cst);
            if (parked.load(std::memory_order_seq_cst) > 0) {
                generation.notify_all();
            }
            return;
        }
        // Spin for at most spinBudget (checking the clock every 64
        // iterations), then park until the generation moves.
        if (spinBudget.count() <= 0) {
            park(gen);
            return;
        }
        using clock = std::chrono::steady_clock;
        clock::time_point deadline{};
        uint32_t iter = 0;
        while (generation.load(std::memory_order_acquire) == gen) {
            if ((++iter & 63) != 0) {
                cpuRelax();
                continue;
            }
            if (iter == 64) {
                deadline = clock::now() + spinBudget;
            } else if (clock::now() >= deadline) {
                park(gen);
                return;
            }
            cpuRelax();
        }
    }

  private:
    /**
     * Sleep until the generation moves past `gen`. The seq_cst increment
     * of `parked` before the wait pairs with the releaser's seq_cst
     * generation store and `parked` load (see arrive_and_wait).
     */
    void
    park(uint32_t gen)
    {
        parked.fetch_add(1, std::memory_order_seq_cst);
        while (generation.load(std::memory_order_seq_cst) == gen) {
            generation.wait(gen, std::memory_order_seq_cst);
        }
        parked.fetch_sub(1, std::memory_order_seq_cst);
    }

    const std::ptrdiff_t expected;
    const std::chrono::nanoseconds spinBudget;
    CompletionFunction completion;
    /** Threads arrived in the current generation (own cache line). */
    alignas(64) std::atomic<std::ptrdiff_t> arrived{0};
    /**
     * Incremented once per completed barrier; waiters spin on it. Only
     * compared for equality, so wrapping around is harmless.
     */
    alignas(64) std::atomic<uint32_t> generation{0};
    /** Waiters sleeping in park(); the releaser notifies only if > 0. */
    alignas(64) std::atomic<uint32_t> parked{0};
};

} // namespace gem5

#endif // __BASE_BARRIER_HH__
