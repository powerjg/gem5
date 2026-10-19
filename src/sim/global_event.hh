/*
 * Copyright (c) 2011-2013 Advanced Micro Devices, Inc.
 * Copyright (c) 2013 Mark D. Hill and David A. Wood
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

#ifndef __SIM_GLOBAL_EVENT_HH__
#define __SIM_GLOBAL_EVENT_HH__

#include <mutex>
#include <vector>

#include "base/logging.hh"
#include "sim/eventq.hh"

namespace gem5
{

/**
 * @file sim/global_event.hh
 * Global events and related declarations.
 *
 * A global event is an event that occurs across all main event queues
 * at the same tick. It consists of a set of local BarrierEvent
 * instances, one per main event queue, and common state. When a queue
 * reaches a local BarrierEvent, EventQueue::serviceOne() returns the
 * BarrierEvent unprocessed to the simulation loop (SimulatorThreads).
 * Once every main event queue has stopped at its local BarrierEvent for
 * the global event, the simulation loop executes the global event's
 * process() method once and releases the local events.
 *
 * There are two basic global event patterns, GlobalEvent and
 * GlobalSyncEvent. GlobalEvent is the base class for typical global
 * events, while GlobalSyncEvent is used for periodic quantum
 * synchronization across multiple main event queues.
 */

/**
 * Common base class for GlobalEvent and GlobalSyncEvent.
 */
class BaseGlobalEvent : public EventBase
{
  private:
      //! Mutex variable for providing exculsive right to schedule global
      //! events. This is necessary so that a total order can be maintained
      //! amongst the global events. Without ensuring the total order, it is
      //! possible that threads execute global events in different orders,
      //! which can result in a deadlock.
      static std::mutex globalQMutex;

  protected:
    /// The base class for the local events scheduled on each main event
    /// queue to pause that queue at the global event tick so the
    /// simulation loop can dispatch the global event across all queues.
    class BarrierEvent : public Event
    {
      protected:
        BaseGlobalEvent *_globalEvent;

        BarrierEvent(BaseGlobalEvent *global_event, Priority p, Flags f)
            : Event(p, f), _globalEvent(global_event)
        {
        }

        ~BarrierEvent();

        friend class BaseGlobalEvent;

      public:
        using Event::release;

        virtual BaseGlobalEvent *
        globalEvent()
        {
            return _globalEvent;
        }

        void
        process() override
        {
            panic("BarrierEvent must be dispatched by the simulation loop");
        }
    };

    //! The individual local event instances (one per thread/event queue).
    std::vector<BarrierEvent *> barrierEvent;

  public:
    static void
    releaseBarrierEvent(Event *event)
    {
        static_cast<BarrierEvent *>(event)->release();
    }

    /**
     * Dispatch an event returned by EventQueue::serviceOne() in a
     * single-queue simulation loop (such as SST or SystemC co-simulation)
     * that does not use SimulatorThreads.
     *
     * If `event` is a BarrierEvent for a BaseGlobalEvent, executes the
     * global event's process() method under mainEventQueue[0]'s lock.
     * Non-exit barrier events are then released and nullptr is returned so
     * the caller continues its loop; exit events are returned to the caller.
     */
    static Event *
    dispatchSingleQueue(Event *event)
    {
        if (!event) {
            return nullptr;
        }
        BaseGlobalEvent *global_event = event->globalEvent();
        if (!global_event) {
            return event;
        }
        panic_if(numMainEventQueues != 1,
                 "BaseGlobalEvent::dispatchSingleQueue requires a single "
                 "main event queue (numMainEventQueues=%u)",
                 numMainEventQueues);
        {
            std::lock_guard<EventQueue> lock(*mainEventQueue[0]);
            curEventQueue(mainEventQueue[0]);
            global_event->process();
        }
        if (event->isExitEvent()) {
            return event;
        }
        releaseBarrierEvent(event);
        return nullptr;
    }

    BaseGlobalEvent(Priority p, Flags f);

    virtual ~BaseGlobalEvent();

    virtual void process() = 0;

    virtual const char *description() const = 0;

    void schedule(Tick when);

    bool scheduled() const
    {
        bool sched = false;
        for (uint32_t i = 0; i < numMainEventQueues; ++i) {
            sched = sched || barrierEvent[i]->scheduled();
        }

        return sched;
    }

    Tick when() const
    {
        assert(numMainEventQueues > 0);
        return barrierEvent[0]->when();
    }

    void deschedule();
    void reschedule(Tick when);
};


/**
 * Funky intermediate class to support CRTP so that we can have a
 * common constructor to create the local events, even though the
 * types of the local events are defined in the derived classes.
 */
template <class Derived>
class BaseGlobalEventTemplate : public BaseGlobalEvent
{
  protected:
    BaseGlobalEventTemplate(Priority p, Flags f)
        : BaseGlobalEvent(p, f)
    {
        for (int i = 0; i < numMainEventQueues; ++i)
            barrierEvent[i] = new typename Derived::BarrierEvent(this, p, f);
    }

    virtual ~BaseGlobalEventTemplate(){}
};

/**
 * The main global event class. Ordinary global events should derive
 * from this class, and define process() to specify the action to be
 * taken when the event is reached. Once all main event queues have
 * reached their local BarrierEvent for this global event, the
 * simulation loop executes process() once before advancing the queues
 * into the next phase.
 */
class GlobalEvent : public BaseGlobalEventTemplate<GlobalEvent>
{
  public:
    typedef BaseGlobalEventTemplate<GlobalEvent> Base;

    class BarrierEvent : public Base::BarrierEvent
    {
      public:
        BarrierEvent(Base *global_event, Priority p, Flags f)
            : Base::BarrierEvent(global_event, p, f)
        { }
    };

    GlobalEvent(Priority p, Flags f)
        : Base(p, f)
    { }

    GlobalEvent(Tick when, Priority p, Flags f)
        : Base(p, f)
    {
        schedule(when);
    }

    virtual void process() = 0;
};

/**
 * A repeating global event that separates simulation quanta in a
 * quantum-based multi-event-queue simulation. Each quantum boundary
 * stops all main event queues so the simulation loop can synchronize
 * worker threads, advance the quantum, and drain asynchronously
 * enqueued cross-queue events at the start of the next phase.
 */
class GlobalSyncEvent : public BaseGlobalEventTemplate<GlobalSyncEvent>
{
  public:
    typedef BaseGlobalEventTemplate<GlobalSyncEvent> Base;

    class BarrierEvent : public Base::BarrierEvent
    {
      public:
        BarrierEvent(Base *global_event, Priority p, Flags f)
            : Base::BarrierEvent(global_event, p, f)
        { }
    };

    GlobalSyncEvent(Priority p, Flags f)
        : Base(p, f), repeat(0)
    { }

    GlobalSyncEvent(Tick when, Tick _repeat, Priority p, Flags f)
        : Base(p, f), repeat(_repeat)
    {
        schedule(when);
        for (int i = 0; i < numMainEventQueues; ++i) {
            mainEventQueue[i]->setNextSimQuantum(when);
        }
    }

    virtual ~GlobalSyncEvent (){}

    void process();

    const char *description() const;

    Tick repeat;
};

} // namespace gem5

#endif // __SIM_GLOBAL_EVENT_HH__
