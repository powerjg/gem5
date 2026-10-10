/*
 * Copyright (c) 2020, 2026 Google LLC
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
#include <mutex>
#include <thread>
#include <vector>

#include "base/fair_adaptive_mutex.hh"

using namespace gem5;

namespace
{

/*
 * Same criterion as the NoBarging test of the FairUncontendedMutex this
 * class replaces: ten threads start together, each yields while holding
 * the lock so that the others queue up behind it, and every thread must
 * get at least half of its fair share of the work.
 */
void
checkNoBarging(FairAdaptiveMutex &m, int total_work = 10000)
{
    const int num_of_thread = 10;
    int remaining_work = total_work;
    std::vector<int> workload(num_of_thread);
    std::vector<std::thread> threads;
    threads.reserve(num_of_thread);

    std::atomic<int> ready_threads = 0;

    for (int t = 0; t < num_of_thread; ++t) {
        threads.emplace_back([&, t]() {
            ++ready_threads;
            while (ready_threads.load() < num_of_thread) {
                std::this_thread::yield();
            }
            while (true) {
                std::scoped_lock<FairAdaptiveMutex> g(m);

                if (remaining_work <= 0) {
                    break;
                }
                --remaining_work;
                ++workload[t];

                std::this_thread::yield();
            }
        });
    }

    for (auto &t : threads) {
        t.join();
    }

    const int minimum_workload = total_work / num_of_thread / 2;
    EXPECT_EQ(remaining_work, 0);
    for (int t = 0; t < num_of_thread; ++t) {
        EXPECT_GT(workload[t], minimum_workload)
            << "Thread " << t << " did not get enough workload.";
    }
}

/* Many threads hammer the lock; every increment must be serialised. */
void
checkHeavyContention(FairAdaptiveMutex &m, int num_of_thread = 100,
                     int num_of_iter = 1000)
{
    std::vector<std::thread> threads;
    threads.reserve(num_of_thread);

    int data = 0;

    for (int t = 0; t < num_of_thread; ++t) {
        threads.emplace_back([&]() {
            for (int k = 0; k < num_of_iter; ++k) {
                std::lock_guard<FairAdaptiveMutex> g(m);
                data++;
            }
        });
    }

    for (auto &t : threads) {
        t.join();
    }
    EXPECT_EQ(data, num_of_iter * num_of_thread);
}

} // anonymous namespace

TEST(FairAdaptiveMutex, NoBarging)
{
    FairAdaptiveMutex m;
    checkNoBarging(m);
}

TEST(FairAdaptiveMutex, NoBargingParking)
{
    FairAdaptiveMutex m(std::chrono::nanoseconds(0));
    checkNoBarging(m, 2000);
}

TEST(FairAdaptiveMutex, HeavyContention)
{
    FairAdaptiveMutex m;
    checkHeavyContention(m);
}

TEST(FairAdaptiveMutex, HeavyContentionSpinning)
{
    FairAdaptiveMutex m(std::chrono::seconds(10));
    checkHeavyContention(m, 2, 10000);
}

TEST(FairAdaptiveMutex, HeavyContentionParking)
{
    FairAdaptiveMutex m(std::chrono::nanoseconds(0));
    checkHeavyContention(m, 8, 500);
}

TEST(FairAdaptiveMutex, SingleThread)
{
    const int num_of_iter = 100000000;
    FairAdaptiveMutex m;
    int data = 0;
    for (int i = 0; i < num_of_iter; i++) {
        std::lock_guard<FairAdaptiveMutex> g(m);
        ++data;
    }
    EXPECT_EQ(data, num_of_iter);
}
