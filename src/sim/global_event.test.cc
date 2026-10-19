/*
 * Copyright (c) 2026 The Regents of The University of California
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "base/gtest/logging.hh"
#include "sim/eventq.hh"
#include "sim/global_event.hh"

namespace gem5
{
namespace
{

class CountingGlobalEvent : public GlobalEvent
{
  public:
    CountingGlobalEvent() : GlobalEvent(Default_Pri, 0) {}

    void
    process() override
    {
        ++processCount;
    }

    const char *
    description() const override
    {
        return "counting global event";
    }

    int processCount = 0;
};

TEST(GlobalEventTest, BarrierEventProcessPanics)
{
    EventQueue *q0 = getEventQueue(0);
    curEventQueue(q0);

    CountingGlobalEvent global_event;
    GlobalEvent::BarrierEvent barrier_event(&global_event, Event::Default_Pri,
                                            0);

    gtestLogOutput.str("");
    EXPECT_THROW(barrier_event.process(), gem5::GTestException);
    EXPECT_THAT(gtestLogOutput.str(),
                ::testing::HasSubstr(
                    "BarrierEvent must be dispatched by the simulation loop"));
}

TEST(GlobalEventTest, ServiceOneReturnsBarrierEventInSerialMode)
{
    for (EventQueue *q : mainEventQueue) {
        delete q;
    }
    mainEventQueue.clear();
    numMainEventQueues = 0;

    EventQueue *q0 = getEventQueue(0);
    curEventQueue(q0);
    inParallelMode = false;

    CountingGlobalEvent global_event;
    global_event.schedule(50);

    Event *ev0 = q0->serviceOne();
    ASSERT_NE(ev0, nullptr);
    EXPECT_EQ(ev0->globalEvent(), &global_event);
    EXPECT_EQ(q0->getCurTick(), 50);
    EXPECT_EQ(global_event.processCount, 0);

    q0->setCurTick(0);
}

TEST(GlobalEventTest, ServiceOneReturnsBarrierEventInParallelMode)
{
    for (EventQueue *q : mainEventQueue) {
        delete q;
    }
    mainEventQueue.clear();
    numMainEventQueues = 0;

    EventQueue *q0 = getEventQueue(0);
    EventQueue *q1 = getEventQueue(1);
    curEventQueue(q0);

    CountingGlobalEvent global_event;
    inParallelMode = true;
    global_event.schedule(100);

    // A single thread can advance both queues to their barrier events
    // without blocking inside serviceOne().
    curEventQueue(q0);
    q0->handleAsyncInsertions();
    Event *ev0 = q0->serviceOne();
    ASSERT_NE(ev0, nullptr);
    EXPECT_EQ(ev0->globalEvent(), &global_event);
    EXPECT_EQ(q0->getCurTick(), 100);
    EXPECT_EQ(global_event.processCount, 0);

    curEventQueue(q1);
    q1->handleAsyncInsertions();
    Event *ev1 = q1->serviceOne();
    ASSERT_NE(ev1, nullptr);
    EXPECT_EQ(ev1->globalEvent(), &global_event);
    EXPECT_EQ(q1->getCurTick(), 100);
    EXPECT_EQ(global_event.processCount, 0);

    inParallelMode = false;
    q0->setCurTick(0);
    q1->setCurTick(0);
    curEventQueue(q0);
}

class AutoDeleteGlobalEvent : public GlobalEvent
{
  public:
    AutoDeleteGlobalEvent(int *process_count, bool *lock_held, bool *destroyed)
        : GlobalEvent(Default_Pri, AutoDelete),
          processCount(process_count),
          lockHeld(lock_held),
          wasDestroyed(destroyed)
    {}

    ~AutoDeleteGlobalEvent() override { *wasDestroyed = true; }

    void
    process() override
    {
        ++(*processCount);
        EventQueue::ScopedRelease release(curEventQueue());
        *lockHeld = true;
    }

    const char *
    description() const override
    {
        return "auto-delete global event";
    }

  private:
    int *processCount;
    bool *lockHeld;
    bool *wasDestroyed;
};

class ExitGlobalEvent : public GlobalEvent
{
  public:
    ExitGlobalEvent() : GlobalEvent(Default_Pri, IsExitEvent) {}

    void
    process() override
    {
        ++processCount;
        EventQueue::ScopedRelease release(curEventQueue());
        lockHeld = true;
    }

    const char *
    description() const override
    {
        return "exit global event";
    }

    int processCount = 0;
    bool lockHeld = false;
};

TEST(GlobalEventTest, DispatchSingleQueueRunsAndReleasesNonExitGlobalEvent)
{
    for (EventQueue *q : mainEventQueue) {
        delete q;
    }
    mainEventQueue.clear();
    numMainEventQueues = 0;

    EventQueue *q0 = getEventQueue(0);
    curEventQueue(q0);
    inParallelMode = false;

    int process_count = 0;
    bool lock_held = false;
    bool destroyed = false;
    auto *global_event =
        new AutoDeleteGlobalEvent(&process_count, &lock_held, &destroyed);
    global_event->schedule(50);

    Event *ev = BaseGlobalEvent::dispatchSingleQueue(q0->serviceOne());
    EXPECT_EQ(ev, nullptr);
    EXPECT_EQ(process_count, 1);
    EXPECT_TRUE(lock_held);
    EXPECT_TRUE(destroyed);

    q0->setCurTick(0);
}

TEST(GlobalEventTest, DispatchSingleQueueRunsAndReturnsExitGlobalEvent)
{
    for (EventQueue *q : mainEventQueue) {
        delete q;
    }
    mainEventQueue.clear();
    numMainEventQueues = 0;

    EventQueue *q0 = getEventQueue(0);
    curEventQueue(q0);
    inParallelMode = false;

    ExitGlobalEvent exit_event;
    exit_event.schedule(75);

    Event *ev = BaseGlobalEvent::dispatchSingleQueue(q0->serviceOne());
    ASSERT_NE(ev, nullptr);
    EXPECT_EQ(ev->globalEvent(), &exit_event);
    EXPECT_EQ(exit_event.processCount, 1);
    EXPECT_TRUE(exit_event.lockHeld);

    q0->setCurTick(0);
}

} // anonymous namespace
} // namespace gem5
