/* -*- coding: utf-8 -*-
 Copyright (c) 2016 Jason Lowe-Power
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are
 met: redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer;
 redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in the
 documentation and/or other materials provided with the distribution;
 neither the name of the copyright holders nor the names of its
 contributors may be used to endorse or promote products derived from
 this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 Authors: Jason Lowe-Power
*/

#include "mem/random_delay_bridge.hh"

#include "base/random.hh"

RandomDelayBridge*
RandomDelayBridgeParams::create()
{
    return new RandomDelayBridge(this);
}


bool
RandomDelayBridge::recvTimingReq(PacketPtr pkt)
{
    if (maxDelay == 0) {
        return masterPort.sendTimingReq(pkt);
    }

    // Align the delay to the clock edge
    Cycles max_cycles = ticksToCycles(maxDelay);
    // Note: "Cycles" isn't actually an integral type...
    // max_cycles will be small (< 10 likely)
    Cycles delay_cycles = Cycles(random_mt.random<int>(0, int(max_cycles)));
    Tick delay = cyclesToTicks(delay_cycles);

    // Add the delay to the packet
    pkt->payloadDelay += delay;

    // Send the request
    bool sent = masterPort.sendTimingReq(pkt);

    // If it fails to send, remove the extra delay we added. The packet
    // will be re-sent by the slave.
    if (!sent) {
        pkt->payloadDelay -= delay;
    }

    return sent;
}
