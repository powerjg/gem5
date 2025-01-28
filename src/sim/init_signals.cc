/*
 * Copyright (c) 2012, 2015 ARM Limited
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
 * Copyright (c) 2000-2005 The Regents of The University of Michigan
 * Copyright (c) 2008 The Hewlett-Packard Development Company
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

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <csignal>
#include <cstring>
#include <iostream>
#include <string>

#if defined(__FreeBSD__)
#include <sys/param.h>

#endif

#include "sim/init_signals.hh"

#include "base/atomicio.hh"
#include "base/cprintf.hh"
#include "base/logging.hh"
#include "sim/async.hh"
#include "sim/backtrace.hh"
#include "sim/eventq.hh"
#include "sim/sim_exit.hh"

namespace gem5
{

// Use an separate stack for fatal signal handlers

static bool
setupAltStack()
{
    const auto stack_size = 2 * SIGSTKSZ;
    static uint8_t *fatal_sig_stack = new uint8_t[stack_size];
    stack_t stack;
#if defined(__FreeBSD__) && (__FreeBSD_version < 1100097)
    stack.ss_sp = (char *)fatal_sig_stack;
#else
    stack.ss_sp = fatal_sig_stack;
#endif
    stack.ss_size = stack_size;
    stack.ss_flags = 0;

    return sigaltstack(&stack, NULL) == 0;
}

static void
installSignalHandler(int signal, void (*handler)(int sigtype),
                     int flags = SA_RESTART,
                     struct sigaction *old_sa = NULL)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = handler;
    sa.sa_flags = flags;

    if (sigaction(signal, &sa, old_sa) == -1)
        panic("Failed to setup handler for signal %i\n", signal);
}

static void
raiseFatalSignal(int signo)
{
    // The signal handler should have been reset and unmasked (it was
    // registered with SA_RESETHAND | SA_NODEFER), just raise the
    // signal again to invoke the default handler.
    STATIC_ERR("For more info on how to address this issue, please visit "
        "https://www.gem5.org/documentation/general_docs/common-errors/ \n\n");
    pthread_kill(pthread_self(), signo);

    // Something is really wrong if the process is alive at this
    // point, manually try to exit it.
    STATIC_ERR("Failed to execute default signal handler!\n");
    _exit(127);
}

/// Stats signal handler.
void
dumpStatsHandler(int sigtype)
{
    async_event = true;
    async_statdump = true;
    /* Wake up some event queue to handle event */
    getEventQueue(0)->wakeup();
}

void
dumprstStatsHandler(int sigtype)
{
    async_event = true;
    async_statdump = true;
    async_statreset = true;
    /* Wake up some event queue to handle event */
    getEventQueue(0)->wakeup();
}

/// Exit signal handler.
void
exitNowHandler(int sigtype)
{
    async_event = true;
    async_exit = true;
    /* Wake up some event queue to handle event */
    getEventQueue(0)->wakeup();
}

/// Abort signal handler.
void
abortHandler(int sigtype)
{
    const EventQueue *const eq(curEventQueue());
    if (eq) {
        ccprintf(std::cerr, "Program aborted at tick %llu\n",
                eq->getCurTick());
    } else {
        STATIC_ERR("Program aborted\n\n");
    }

    print_backtrace();
    raiseFatalSignal(sigtype);
}

/// Segmentation fault signal handler.
static void
segvHandler(int sigtype)
{
    STATIC_ERR("gem5 has encountered a segmentation fault!\n\n");

    print_backtrace();
    raiseFatalSignal(SIGSEGV);
}

// Handle SIGIO
static void
ioHandler(int sigtype)
{
    async_event = true;
    async_io = true;
    /* Wake up some event queue to handle event */
    getEventQueue(0)->wakeup();
}

// Handle signals from external processes
static void
externalProcessHandler(int sigtype)
{
    async_event = true;

    std::string shared_mem_name_str = "shared_gem5_signal_mem_" +
        std::to_string(getpid());
    const char* shared_mem_name = shared_mem_name_str.c_str();
    const std::size_t shared_mem_size = 4096;

    int shm_fd = shm_open(shared_mem_name, O_RDWR, 0666); //0666 = rw-rw-rw-
    if (shm_fd == -1) {
        std::cerr << "Error: Unable to open shared memory" << std::endl;
        return;
    }

    void* shm_ptr = mmap(0, shared_mem_size, PROT_READ | PROT_WRITE,
        MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED) {
        std::cerr << "Error: Unable to map shared memory" << std::endl;
        close(shm_fd);
        return;
    }

    char full_payload[shared_mem_size];
    std::memcpy(full_payload, shm_ptr, shared_mem_size);
    full_payload[shared_mem_size - 1] = '\0';  // Ensure null-termination

    std::cout << "Received signal from external process with payload: '"
        << full_payload << "'" << std::endl;

    // process payload json with string processing
    std::string full_payload_str = full_payload;
    std::map<std::string, std::string> payload_map;

    //get the hypercall id
    std::size_t payload_pos = 0;
    std::string hypercall_id_str = extractStringFromJSON(full_payload_str,
        "\"id\":", ",", payload_pos);
    uint64_t hypercall_id = std::stoi(hypercall_id_str);

    // parse the payload. Start looking for key-value pairs after `"payload":`
    std::string payload_key = "\"payload\":";
    payload_pos = full_payload_str.find(payload_key) + payload_key.length();

    while (full_payload_str.find('"', payload_pos) != std::string::npos){
        std::string key = extractStringFromJSON(full_payload_str, "\"", "\":",
            payload_pos);
        std::string value = extractStringFromJSON(full_payload_str, "\"", "\"",
            payload_pos);
        payload_map[key] = value;
    }

    // put a "done" message into the shared memory so the transmitter knows to
    // close and unlink the memory on its end.
    char done_msg[shared_mem_size] = "done";
    done_msg[shared_mem_size - 1] = '\0';  // Ensure null-termination
    std::memcpy(shm_ptr, done_msg, sizeof(done_msg));

    munmap(shm_ptr, shared_mem_size);
    close(shm_fd);

    exitSimulationLoopNow(hypercall_id, payload_map);
}

std::string
extractStringFromJSON(std::string& full_str, std::string start_str,
    std::string end_str, std::size_t& search_start)
{
    std::size_t start = full_str.find(start_str, search_start) +
        start_str.size();
    std::size_t end = full_str.find(end_str, start);

    // move position in payload past current key or value
    search_start = end + end_str.size();
    return (full_str.substr(start, end - start));
}

/*
 * M5 can do several special things when various signals are sent.
 * None are mandatory.
 */
void
initSignals()
{
    // Floating point exceptions may happen on misspeculated paths, so
    // ignore them
    signal(SIGFPE, SIG_IGN);

    // Dump intermediate stats
    installSignalHandler(SIGUSR1, dumpStatsHandler);

    // Dump intermediate stats and reset them
    installSignalHandler(SIGUSR2, dumprstStatsHandler);

    // Print the current cycle number and a backtrace on abort. Make
    // sure the signal is unmasked and the handler reset when a signal
    // is delivered to be able to invoke the default handler.
    installSignalHandler(SIGABRT, abortHandler, SA_RESETHAND | SA_NODEFER);

    // Setup a SIGSEGV handler with a private stack
    if (setupAltStack()) {
        installSignalHandler(SIGSEGV, segvHandler,
                             SA_RESETHAND | SA_NODEFER | SA_ONSTACK);
    } else {
        warn("Failed to setup stack for SIGSEGV handler, "
             "using default signal handler.\n");
    }

    // Install a SIGIO handler to handle asynchronous file IO. See the
    // PollQueue class.
    installSignalHandler(SIGIO, ioHandler);
}

void initSigRtmin()
{
    installSignalHandler(SIGRTMIN, externalProcessHandler);
}

struct sigaction old_int_sa;

void initSigInt()
{
    // Exit cleanly on Interrupt (Ctrl-C)
    installSignalHandler(SIGINT, exitNowHandler, SA_RESTART, &old_int_sa);
}

void restoreSigInt()
{
    // Restore the old SIGINT handler
    sigaction(SIGINT, &old_int_sa, NULL);
}


} // namespace gem5
