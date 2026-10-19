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

#include <cstdint>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "sim/eventq.hh"
#include "sim/global_event.hh"
#include "sim/sim_threads.hh"

namespace gem5
{
namespace
{

class SimThreadsTest : public ::testing::Test
{
  protected:
    void
    setupQueues(uint32_t count)
    {
        cleanupQueues();
        for (uint32_t i = 0; i < count; ++i) {
            getEventQueue(i);
        }
        curEventQueue(mainEventQueue[0]);
        inParallelMode = (count > 1);
    }

    void
    cleanupQueues()
    {
        inParallelMode = false;
        for (EventQueue *q : mainEventQueue) {
            delete q;
        }
        mainEventQueue.clear();
        numMainEventQueues = 0;
        curEventQueue(nullptr);
    }

    void
    TearDown() override
    {
        cleanupQueues();
    }
};

class LocalTickEvent : public Event
{
  public:
    LocalTickEvent(Tick *recorded_tick)
        : Event(Default_Pri, AutoDelete), recordedTick(recorded_tick)
    {}

    void
    process() override
    {
        *recordedTick = curTick();
    }

    const char *
    description() const override
    {
        return "local tick event";
    }

  private:
    Tick *recordedTick;
};

class CheckingGlobalEvent : public GlobalEvent
{
  public:
    CheckingGlobalEvent(Tick expected_tick)
        : GlobalEvent(Default_Pri, 0), expectedTick(expected_tick)
    {}

    void
    process() override
    {
        ++processCount;
        allQueuesAtExpectedTick = true;
        for (uint32_t i = 0; i < numMainEventQueues; ++i) {
            if (mainEventQueue[i]->getCurTick() != expectedTick) {
                allQueuesAtExpectedTick = false;
            }
        }
    }

    const char *
    description() const override
    {
        return "checking global event";
    }

    Tick expectedTick;
    int processCount = 0;
    bool allQueuesAtExpectedTick = false;
};

class TestExitGlobalEvent : public GlobalEvent
{
  public:
    TestExitGlobalEvent(Tick when)
        : GlobalEvent(when, Sim_Exit_Pri, IsExitEvent)
    {}

    void
    process() override
    {
        ++processCount;
    }

    const char *
    description() const override
    {
        return "test global exit event";
    }

    int processCount = 0;
};

TEST_F(SimThreadsTest, DynamicDispatchProcessesGlobalEventsOnce)
{
    constexpr uint32_t num_queues = 6;
    constexpr uint32_t num_threads = 2;
    setupQueues(num_queues);

    for (uint32_t i = 0; i < num_queues; ++i) {
        mainEventQueue[i]->setNextSimQuantum(100);
    }
    GlobalSyncEvent sync_event(100, 100, EventBase::Minimum_Pri, 0);

    std::vector<Tick> local_ticks(num_queues, 0);
    for (uint32_t i = 0; i < num_queues; ++i) {
        Tick target_tick = 50 + i * 60;
        mainEventQueue[i]->schedule(new LocalTickEvent(&local_ticks[i]),
                                    target_tick);
    }

    CheckingGlobalEvent check_event(250);
    check_event.schedule(250);

    TestExitGlobalEvent exit_event(400);

    SimulatorThreads threads(num_queues, num_threads);
    Event *local_exit = threads.runUntilLocalExit();

    ASSERT_NE(local_exit, nullptr);
    EXPECT_EQ(local_exit->globalEvent(), &exit_event);
    EXPECT_EQ(exit_event.processCount, 1);
    EXPECT_EQ(check_event.processCount, 1);
    EXPECT_TRUE(check_event.allQueuesAtExpectedTick);

    for (uint32_t i = 0; i < num_queues; ++i) {
        EXPECT_EQ(local_ticks[i], 50 + i * 60);
        EXPECT_EQ(mainEventQueue[i]->getCurTick(), 400);
    }

    sync_event.deschedule();
}

TEST_F(SimThreadsTest, StaticMappingBindsWorkerToQueue)
{
    constexpr uint32_t num_queues = 4;
    setupQueues(num_queues);

    for (uint32_t i = 0; i < num_queues; ++i) {
        mainEventQueue[i]->setNextSimQuantum(100);
    }
    GlobalSyncEvent sync_event(100, 100, EventBase::Minimum_Pri, 0);
    TestExitGlobalEvent exit_event(400);

    std::mutex record_mutex;
    std::vector<std::vector<std::thread::id>> queue_threads(num_queues);

    auto service_fn = [&](EventQueue *eq) -> Event * {
        uint32_t q_idx = 0;
        for (uint32_t i = 0; i < num_queues; ++i) {
            if (mainEventQueue[i] == eq) {
                q_idx = i;
                break;
            }
        }
        {
            std::lock_guard<std::mutex> guard(record_mutex);
            queue_threads[q_idx].push_back(std::this_thread::get_id());
        }
        return SimulatorThreads::defaultServiceQueue(eq);
    };

    const std::thread::id main_tid = std::this_thread::get_id();
    // num_threads == 0 selects the default N == M static mapping.
    SimulatorThreads threads(num_queues, 0, service_fn);
    Event *local_exit = threads.runUntilLocalExit();

    ASSERT_NE(local_exit, nullptr);
    EXPECT_EQ(local_exit->globalEvent(), &exit_event);

    std::set<std::thread::id> distinct_threads;
    for (uint32_t q = 0; q < num_queues; ++q) {
        // 4 sync phases (ticks 100, 200, 300, 400) + 1 exit phase (tick 400).
        ASSERT_EQ(queue_threads[q].size(), 5u);
        const std::thread::id first_tid = queue_threads[q][0];
        distinct_threads.insert(first_tid);
        for (const std::thread::id &tid : queue_threads[q]) {
            EXPECT_EQ(tid, first_tid);
        }
    }
    EXPECT_EQ(queue_threads[0][0], main_tid);
    EXPECT_EQ(distinct_threads.size(), num_queues);

    sync_event.deschedule();
}

TEST_F(SimThreadsTest, GlobalExitEventEndsPassAndIsReturnedUnreleased)
{
    constexpr uint32_t num_queues = 3;
    setupQueues(num_queues);

    for (uint32_t i = 0; i < num_queues; ++i) {
        mainEventQueue[i]->setNextSimQuantum(100);
    }
    GlobalSyncEvent sync_event(100, 100, EventBase::Minimum_Pri, 0);
    TestExitGlobalEvent exit_event(200);

    std::vector<Tick> later_ticks(num_queues, 0);
    for (uint32_t i = 0; i < num_queues; ++i) {
        mainEventQueue[i]->schedule(new LocalTickEvent(&later_ticks[i]), 300);
    }

    SimulatorThreads threads(num_queues, 2);
    Event *local_exit = threads.runUntilLocalExit();

    ASSERT_NE(local_exit, nullptr);
    EXPECT_TRUE(local_exit->isExitEvent());
    EXPECT_EQ(local_exit->globalEvent(), &exit_event);
    EXPECT_EQ(exit_event.processCount, 1);

    for (uint32_t i = 0; i < num_queues; ++i) {
        EXPECT_EQ(mainEventQueue[i]->getCurTick(), 200);
        EXPECT_EQ(later_ticks[i], 0);
        EXPECT_FALSE(mainEventQueue[i]->empty());
    }

    sync_event.deschedule();
}

class OrderedAutoDeleteGlobalEvent
    : public BaseGlobalEventTemplate<OrderedAutoDeleteGlobalEvent>
{
  public:
    using Base = BaseGlobalEventTemplate<OrderedAutoDeleteGlobalEvent>;

    class BarrierEvent : public Base::BarrierEvent
    {
      public:
        BarrierEvent(Base *global_event, Priority p, Flags f)
            : Base::BarrierEvent(global_event, p, f)
        {}

        ~BarrierEvent() override
        {
            auto *parent =
                static_cast<OrderedAutoDeleteGlobalEvent *>(_globalEvent);
            uint32_t slot = 0;
            for (uint32_t i = 0; i < numMainEventQueues; ++i) {
                if (parent->barrierEvent[i] == this) {
                    slot = i;
                    break;
                }
            }
            parent->releaseOrder->push_back(slot);
        }
    };

    OrderedAutoDeleteGlobalEvent(Tick when,
                                 std::vector<uint32_t> *release_order,
                                 size_t *slots_released_before_global_delete)
        : Base(Default_Pri, AutoDelete),
          releaseOrder(release_order),
          slotsReleasedBeforeGlobalDelete(slots_released_before_global_delete)
    {
        schedule(when);
    }

    ~OrderedAutoDeleteGlobalEvent() override
    {
        *slotsReleasedBeforeGlobalDelete = releaseOrder->size();
    }

    void
    process() override
    {}

    const char *
    description() const override
    {
        return "ordered autodelete global event";
    }

    std::vector<uint32_t> *releaseOrder;
    size_t *slotsReleasedBeforeGlobalDelete;
};

TEST_F(SimThreadsTest, AutoDeleteGlobalEventReleasesSlotZeroLast)
{
    constexpr uint32_t num_queues = 4;
    setupQueues(num_queues);

    for (uint32_t i = 0; i < num_queues; ++i) {
        mainEventQueue[i]->setNextSimQuantum(100);
    }
    GlobalSyncEvent sync_event(100, 100, EventBase::Minimum_Pri, 0);

    std::vector<uint32_t> release_order;
    size_t slots_before_delete = 0;
    new OrderedAutoDeleteGlobalEvent(150, &release_order,
                                     &slots_before_delete);

    TestExitGlobalEvent exit_event(200);

    SimulatorThreads threads(num_queues, 2);
    Event *local_exit = threads.runUntilLocalExit();

    ASSERT_NE(local_exit, nullptr);
    EXPECT_EQ(local_exit->globalEvent(), &exit_event);
    const std::vector<uint32_t> expected_order = {3, 2, 1, 0};
    EXPECT_EQ(release_order, expected_order);
    EXPECT_EQ(slots_before_delete, num_queues);

    sync_event.deschedule();
}

TEST_F(SimThreadsTest, SecondPassOnSameSimulatorThreadsSucceeds)
{
    constexpr uint32_t num_queues = 4;
    setupQueues(num_queues);

    for (uint32_t i = 0; i < num_queues; ++i) {
        mainEventQueue[i]->setNextSimQuantum(100);
    }
    GlobalSyncEvent sync_event(100, 100, EventBase::Minimum_Pri, 0);

    TestExitGlobalEvent exit1(200);
    SimulatorThreads threads(num_queues, 2);
    Event *ev1 = threads.runUntilLocalExit();
    ASSERT_NE(ev1, nullptr);
    EXPECT_EQ(ev1->globalEvent(), &exit1);
    for (uint32_t i = 0; i < num_queues; ++i) {
        EXPECT_EQ(mainEventQueue[i]->getCurTick(), 200);
    }

    std::vector<Tick> pass2_ticks(num_queues, 0);
    for (uint32_t i = 0; i < num_queues; ++i) {
        mainEventQueue[i]->schedule(new LocalTickEvent(&pass2_ticks[i]), 350);
    }
    TestExitGlobalEvent exit2(500);

    Event *ev2 = threads.runUntilLocalExit();
    ASSERT_NE(ev2, nullptr);
    EXPECT_EQ(ev2->globalEvent(), &exit2);
    for (uint32_t i = 0; i < num_queues; ++i) {
        EXPECT_EQ(pass2_ticks[i], 350);
        EXPECT_EQ(mainEventQueue[i]->getCurTick(), 500);
    }

    sync_event.deschedule();
}

class ScopedReleaseGlobalEvent : public GlobalEvent
{
  public:
    ScopedReleaseGlobalEvent(Tick when, bool *lock_was_held)
        : GlobalEvent(when, Default_Pri, 0), lockWasHeld(lock_was_held)
    {}

    void
    process() override
    {
        // DistIface::SyncEvent::process() constructs
        // EventQueue::ScopedRelease(curEventQueue()), which unlocks and
        // relocks curEventQueue(). Verify mainEventQueue[0] is locked
        // when finishPhase() invokes global_event->process().
        EventQueue::ScopedRelease release(curEventQueue());
        *lockWasHeld = true;
    }

    const char *
    description() const override
    {
        return "scoped release global event";
    }

  private:
    bool *lockWasHeld;
};

TEST_F(SimThreadsTest, GlobalEventProcessRunsWithMainQueueZeroLocked)
{
    setupQueues(2);

    bool lock_was_held = false;
    ScopedReleaseGlobalEvent scoped_release_event(100, &lock_was_held);
    TestExitGlobalEvent exit_event(200);

    SimulatorThreads dispatcher(2, 2);
    Event *exit_ev = dispatcher.runUntilLocalExit();

    ASSERT_NE(exit_ev, nullptr);
    EXPECT_EQ(exit_ev->globalEvent(), &exit_event);
    EXPECT_TRUE(lock_was_held);
}

} // anonymous namespace
} // namespace gem5
