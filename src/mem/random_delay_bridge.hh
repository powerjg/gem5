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

#ifndef __MEM_RANDOM_DELAY_BRIDGE_HH__
#define __MEM_RANDOM_DELAY_BRIDGE_HH__

#include "mem/mem_object.hh"
#include "params/RandomDelayBridge.hh"

class RandomDelayBridge : public MemObject
{
    Tick maxDelay;

  public:

    /** Parameters of communication monitor */
    typedef RandomDelayBridgeParams Params;

    /**
     * Constructor based on the Python params
     *
     * @param params Python parameters
     */
    RandomDelayBridge(Params* params) :
        MemObject(params), maxDelay(params->max_delay),
        masterPort(name() + "-master", *this),
        slavePort(name() + "-slave", *this)
    { }

    BaseMasterPort& getMasterPort(const std::string& if_name,
                                  PortID idx = InvalidPortID) override
    {
        if (if_name == "master") {
            return masterPort;
        } else {
            return MemObject::getMasterPort(if_name, idx);
        }
    }

    BaseSlavePort& getSlavePort(const std::string& if_name,
                                PortID idx = InvalidPortID) override
    {
        if (if_name == "slave") {
            return slavePort;
        } else {
            return MemObject::getSlavePort(if_name, idx);
        }
    }

  private:
    class RDBMasterPort : public MasterPort
    {
        RandomDelayBridge& rdb;

      public:
        RDBMasterPort(const std::string& name, RandomDelayBridge &rdb) :
            MasterPort(name, &rdb), rdb(rdb)
        { }

      protected:
        void recvFunctionalSnoop(PacketPtr pkt)
            { rdb.recvFunctionalSnoop(pkt); }

        Tick recvAtomicSnoop(PacketPtr pkt)
            { return rdb.recvAtomicSnoop(pkt); }

        bool recvTimingResp(PacketPtr pkt)
            { return rdb.recvTimingResp(pkt); }

        void recvTimingSnoopReq(PacketPtr pkt)
            { rdb.recvTimingSnoopReq(pkt); }

        void recvRangeChange()
            { rdb.recvRangeChange(); }

        bool isSnooping() const
            { return rdb.isSnooping(); }

        void recvReqRetry()
            { rdb.recvReqRetry(); }

        void recvRetrySnoopResp()
            { rdb.recvRetrySnoopResp(); }
    };

    class RDBSlavePort : public SlavePort
    {
        RandomDelayBridge& rdb;

      public:

        RDBSlavePort(const std::string& name, RandomDelayBridge &rdb) :
            SlavePort(name, &rdb), rdb(rdb)
        { }

      protected:

        void recvFunctional(PacketPtr pkt)
            { rdb.recvFunctional(pkt); }

        Tick recvAtomic(PacketPtr pkt)
            { return rdb.recvAtomic(pkt); }

        bool recvTimingReq(PacketPtr pkt)
            { return rdb.recvTimingReq(pkt); }

        bool recvTimingSnoopResp(PacketPtr pkt)
            { return rdb.recvTimingSnoopResp(pkt); }

        AddrRangeList getAddrRanges() const
            { return rdb.getAddrRanges(); }

        void recvRespRetry()
            { rdb.recvRespRetry(); }
    };

    RDBMasterPort masterPort;
    RDBSlavePort slavePort;

    void recvFunctional(PacketPtr pkt)
        { masterPort.sendFunctional(pkt); }

    void recvFunctionalSnoop(PacketPtr pkt)
        { slavePort.sendFunctionalSnoop(pkt); }

    Tick recvAtomic(PacketPtr pkt)
        { return masterPort.sendAtomic(pkt); }

    Tick recvAtomicSnoop(PacketPtr pkt)
        { return slavePort.sendAtomicSnoop(pkt); }

    bool recvTimingReq(PacketPtr pkt);

    bool recvTimingResp(PacketPtr pkt)
        { return slavePort.sendTimingResp(pkt); }

    void recvTimingSnoopReq(PacketPtr pkt)
        { slavePort.sendTimingSnoopReq(pkt); }

    bool recvTimingSnoopResp(PacketPtr pkt)
        { return masterPort.sendTimingSnoopResp(pkt); }

    void recvRetrySnoopResp()
        { slavePort.sendRetrySnoopResp(); }

    AddrRangeList getAddrRanges() const
        { return masterPort.getAddrRanges(); }

    bool isSnooping() const
        { return slavePort.isSnooping(); }

    void recvReqRetry()
        { slavePort.sendRetryReq(); }

    void recvRespRetry()
        { masterPort.sendRetryResp(); }

    void recvRangeChange()
        { slavePort.sendRangeChange(); }

};

#endif
