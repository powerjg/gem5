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

#ifndef __SIM_SIM_THREADS_HH__
#define __SIM_SIM_THREADS_HH__

#include <atomic>
#include <barrier>
#include <cassert>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "base/barrier.hh"
#include "base/logging.hh"
#include "sim/eventq.hh"
#include "sim/global_event.hh"

namespace gem5
{

/**
 * Coordinates simulation execution across M main event queues using a pool of
 * N <= M host worker threads (the calling thread as worker 0 plus N - 1
 * subordinate worker threads).
 *
 * When N == M (the default when Root.num_threads is 0 or >= M), worker w
 * statically services mainEventQueue[w] in every phase, preserving the fixed
 * thread-to-queue affinity required by KVM CPUs and host cache locality.
 * When N < M, workers dynamically self-schedule queues from [0, M) off the
 * shared atomic index nextQueue in each phase. In both modes, all workers
 * synchronize at phaseBarrier whenever the queues reach a global event
 * (quantum sync, stat dump, debug break, or simulation loop exit), and
 * finishPhase() dispatches the global event once on mainEventQueue[0].
 */
class SimulatorThreads
{
  public:
    using QueueServiceFn = std::function<Event *(EventQueue *)>;

    SimulatorThreads() = delete;
    SimulatorThreads(const SimulatorThreads &) = delete;
    SimulatorThreads &operator=(SimulatorThreads &) = delete;

    SimulatorThreads(uint32_t num_queues, uint32_t num_threads = 0,
                     QueueServiceFn service_queue = defaultServiceQueue)
        : terminate(false),
          numQueues(num_queues),
          numThreads(selectNumThreads(num_queues, num_threads)),
          serviceQueue(std::move(service_queue)),
          nextQueue(0),
          phaseEvents(num_queues, nullptr),
          phaseExitEvent(nullptr),
          phaseExit(false),
          barrier(numThreads),
          phaseBarrier(numThreads, FinishPhase{this})
    {
        threads.reserve(numThreads - 1);
    }

    ~SimulatorThreads()
    {
        // This should only happen after exit has been
        // called. Subordinate event queues should normally (assuming
        // exit is called from Python) be waiting on the barrier when
        // this happens.
        //
        // N.B.: Not terminating here would make it impossible to
        // safely destroy the barrier.
        terminateThreads();
    }

    Event *
    runUntilLocalExit()
    {
        assert(!terminate);

        // Start subordinate threads if needed.
        if (threads.empty() && numThreads > 1) {
            // The main thread (the one running Python) is worker 0 and
            // participates alongside numThreads - 1 subordinate workers.
            for (uint32_t i = 1; i < numThreads; i++) {
                threads.emplace_back([this, i]() { thread_main(i); });
            }
        }

        nextQueue = 0;
        phaseExitEvent = nullptr;
        phaseExit = false;

        // This method is called from the main thread. All subordinate
        // threads are waiting on the barrier when the function is called.
        // The arrival of the main thread here satisfies the barrier and
        // starts another simulation pass across the worker pool.
        barrier.arrive_and_wait();
        runPhases(0);
        barrier.arrive_and_wait();

        curEventQueue(mainEventQueue[0]);
        return phaseExitEvent;
    }

    void
    terminateThreads()
    {
        assert(!terminate);
        if (threads.empty()) {
            return;
        }

        /* This function should only be called when the simulator is
         * handling a global exit event (typically from Python). This
         * means that the helper threads will be waiting on the
         * barrier. Tell the helper threads to exit and release them from
         * their barrier. */
        terminate = true;
        barrier.arrive_and_wait();

        /* Wait for all of the threads to terminate */
        for (auto &t : threads) {
            t.join();
        }

        terminate = false;
        threads.clear();
    }

    static Event *
    defaultServiceQueue(EventQueue *eventq)
    {
        curEventQueue(eventq);
        {
            // Lock *eventq while draining async_queue at the start of the
            // quantum. In dynamic dispatch (numThreads < numQueues), another
            // worker thread already servicing queue 1 in the new quantum may
            // enqueue a cross-queue event onto queue 2's async_queue before
            // queue 2 has drained its async_queue from the previous quantum.
            // Holding *eventq serializes that insertion against
            // handleAsyncInsertions(), and any newly inserted cross-queue
            // event is guaranteed to have a timestamp in at least the next
            // quantum (never the current quantum).
            std::lock_guard<EventQueue> lock(*eventq);
            eventq->handleAsyncInsertions();
        }
        while (!eventq->empty()) {
            if (Event *ev = eventq->serviceOne()) {
                return ev;
            }
        }
        return nullptr;
    }

  protected:
    struct FinishPhase
    {
        SimulatorThreads *simThreads;
        void
        operator()() const noexcept
        {
            simThreads->finishPhase();
        }
    };

    static uint32_t
    selectNumThreads(uint32_t num_queues, uint32_t requested_threads)
    {
        assert(num_queues >= 1);
        if (requested_threads > num_queues) {
            warn("Requested %u simulator threads for %u main event queues; "
                 "clamping to %u\n",
                 requested_threads, num_queues, num_queues);
            return num_queues;
        }
        if (requested_threads == 0) {
            return num_queues;
        }
        return requested_threads;
    }

    /**
     * Advance all main event queues through one or more global-event
     * phases (quanta, stat dumps, debug breaks, or exits) until a global
     * exit event or async_exception terminates the simulation pass.
     *
     * When numThreads == numQueues, worker_id statically services
     * mainEventQueue[worker_id] in every phase. When numThreads < numQueues,
     * the N worker threads dynamically self-schedule queues from
     * [0, numQueues) off the shared atomic counter nextQueue. For each
     * queue q, serviceQueue() processes events until EventQueue::serviceOne()
     * encounters a BaseGlobalEvent::BarrierEvent and returns it unprocessed,
     * storing that barrier event in phaseEvents[q]. Once all numQueues
     * queues have stopped at their barrier event for the phase, all N
     * worker threads synchronize at phaseBarrier, which invokes
     * finishPhase() on the last arriving thread before releasing the
     * workers into the next phase (or exiting the loop if phaseExit is
     * set).
     */
    void
    runPhases(uint32_t worker_id)
    {
        do {
            // Static 1:1 thread-to-queue mapping when numThreads == numQueues.
            // This is required for KVM CPUs (which bind vCPU signal delivery,
            // per-thread perf counters, and timers to the host OS thread that
            // services the CPU's event queue) to always keep the same thread
            // on the same queue across phases.
            if (numThreads == numQueues) {
                phaseEvents[worker_id] =
                    serviceQueue(mainEventQueue[worker_id]);
            } else {
                while (true) {
                    uint32_t q = nextQueue++;
                    if (q >= numQueues) {
                        break;
                    }
                    phaseEvents[q] = serviceQueue(mainEventQueue[q]);
                }
            }
            phaseBarrier.arrive_and_wait();
        } while (!phaseExit);
    }

    /**
     * Completion function for phaseBarrier. Runs single-threaded on the
     * last worker thread to arrive at the end of a phase while all other
     * worker threads are blocked at phaseBarrier.
     */
    void
    finishPhase() noexcept
    {
        // Reset the dynamic-dispatch queue counter for the next phase
        // before phaseBarrier releases the worker threads.
        nextQueue = 0;

        // doSimLoop() only returns nullptr when mainEventQueue[0] exits
        // early due to an async_exception (e.g., a Python exception).
        // Signal all workers to exit runPhases() and return nullptr to
        // simulate().
        Event *first = phaseEvents[0];
        if (!first) {
            phaseExitEvent = nullptr;
            phaseExit = true;
            return;
        }

        // Global events are scheduled across all queues under
        // BaseGlobalEvent::globalQMutex, so every queue must have stopped
        // at its local BarrierEvent for the exact same BaseGlobalEvent.
        BaseGlobalEvent *global_event = first->globalEvent();
        panic_if(!global_event,
                 "Queue 0 stopped at non-global event '%s' (%s)",
                 first->name().c_str(), first->description());
        for (uint32_t i = 1; i < numQueues; ++i) {
            Event *ev = phaseEvents[i];
            panic_if(!ev || ev->globalEvent() != global_event,
                     "Queue %u stopped at '%s' (%s) instead of global event "
                     "'%s'",
                     i, ev ? ev->name().c_str() : "null",
                     ev ? ev->description() : "null",
                     global_event->description());
        }

        // Execute the global event action (e.g., GlobalSyncEvent::process()
        // to advance the quantum, or GlobalSimLoopExitEvent::process())
        // once on mainEventQueue[0]. Acquire mainEventQueue[0]'s service
        // lock because global event process() implementations (such as
        // DistIface::SyncEvent::process()) may construct
        // EventQueue::ScopedRelease(curEventQueue()).
        curEventQueue(mainEventQueue[0]);
        {
            std::lock_guard<EventQueue> lock(*mainEventQueue[0]);
            global_event->process();
        }

        if (first->isExitEvent()) {
            // Retain the exit event for simulate() to inspect and return;
            // simulate() cleans it up on the next entry.
            phaseExitEvent = first;
            phaseExit = true;
        } else {
            // Release each queue's local BarrierEvent in reverse order so
            // slot 0 (whose BarrierEvent::~BarrierEvent deletes an
            // AutoDelete BaseGlobalEvent) runs after slots 1..numQueues-1
            // have finished accessing their BarrierEvents.
            for (uint32_t i = numQueues; i-- > 0;) {
                BaseGlobalEvent::releaseBarrierEvent(phaseEvents[i]);
            }
        }
    }

    /**
     * The main function for all subordinate worker threads (workers
     * 1..numThreads-1). Each pass waits on barrier, services event queues
     * phase-by-phase alongside worker 0 until a global exit event is
     * reached, and synchronizes at barrier before returning to idle.
     */
    void
    thread_main(uint32_t worker_id)
    {
        while (true) {
            barrier.arrive_and_wait();
            if (terminate) {
                break;
            }
            runPhases(worker_id);
            barrier.arrive_and_wait();
        }
    }

    std::atomic<bool> terminate;
    uint32_t numQueues;
    uint32_t numThreads;
    QueueServiceFn serviceQueue;
    std::atomic<uint32_t> nextQueue;
    std::vector<Event *> phaseEvents;
    Event *phaseExitEvent;
    bool phaseExit;
    std::vector<std::thread> threads;
    std::barrier<> barrier;
    Barrier<FinishPhase> phaseBarrier;
};

} // namespace gem5

#endif // __SIM_SIM_THREADS_HH__
