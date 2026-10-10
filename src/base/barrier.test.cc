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

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "base/barrier.hh"

using namespace gem5;

namespace
{

struct CountCompletions
{
    std::atomic<int> *completions;
    void
    operator()() const noexcept
    {
        completions->fetch_add(1);
    }
};

/*
 * Every thread increments a per-round counter, then enters the barrier.
 * After the barrier all increments of the round must be visible and the
 * completion function must have run exactly once per round.
 */
void
checkRoundsStayInLockstep(Barrier<CountCompletions> &barrier,
                          std::atomic<int> &completions, int num_threads,
                          int num_rounds)
{
    std::vector<std::atomic<int>> arrivals(num_rounds);
    for (auto &a : arrivals) {
        a.store(0);
    }

    std::atomic<int> errors{0};
    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&]() {
            for (int r = 0; r < num_rounds; ++r) {
                arrivals[r].fetch_add(1, std::memory_order_release);
                barrier.arrive_and_wait();
                if (arrivals[r].load(std::memory_order_acquire) !=
                    num_threads) {
                    errors.fetch_add(1);
                }
                if (completions.load() != r + 1) {
                    errors.fetch_add(1);
                }
            }
        });
    }
    for (auto &t : threads) {
        t.join();
    }

    EXPECT_EQ(errors.load(), 0);
    EXPECT_EQ(completions.load(), num_rounds);
}

} // anonymous namespace

/* Default budget (spins up to 100 microseconds, then parks). */
TEST(Barrier, RoundsStayInLockstep)
{
    std::atomic<int> completions{0};
    Barrier<CountCompletions> barrier(8, CountCompletions{&completions});
    checkRoundsStayInLockstep(barrier, completions, 8, 2000);
}

/* Spin path: a budget long enough that no waiter parks. */
TEST(Barrier, RoundsStayInLockstepSpinning)
{
    std::atomic<int> completions{0};
    Barrier<CountCompletions> barrier(8, CountCompletions{&completions},
                                      std::chrono::seconds(10));
    checkRoundsStayInLockstep(barrier, completions, 8, 2000);
}

/* Park path: a zero budget makes every waiter sleep in the kernel. */
TEST(Barrier, RoundsStayInLockstepParking)
{
    std::atomic<int> completions{0};
    Barrier<CountCompletions> barrier(8, CountCompletions{&completions},
                                      std::chrono::nanoseconds(0));
    checkRoundsStayInLockstep(barrier, completions, 8, 500);
}

/* A single thread passes through without waiting. */
TEST(Barrier, SingleThread)
{
    std::atomic<int> completions{0};
    Barrier<CountCompletions> barrier(1, CountCompletions{&completions});
    for (int i = 0; i < 1000; ++i) {
        barrier.arrive_and_wait();
    }
    EXPECT_EQ(completions.load(), 1000);
}
