#!/usr/bin/env python3

import json
import os
import signal
import sys
from multiprocessing import shared_memory
from typing import Optional

SHARED_MEM_NAME = "shared_gem5_signal_mem"
SHARED_MEM_SIZE = 4096


def send_signal(pid: int, id: int, payload: str) -> None:
    try:
        shm = shared_memory.SharedMemory(
            name=SHARED_MEM_NAME, create=True, size=SHARED_MEM_SIZE
        )
    except FileExistsError:
        shm = shared_memory.SharedMemory(name=SHARED_MEM_NAME)

    shm.buf[:SHARED_MEM_SIZE] = b"\x00" * SHARED_MEM_SIZE
    final_payload = create_json(id, payload)
    shm.buf[: len(final_payload.encode())] = final_payload.encode()
    os.kill(pid, signal.SIGRTMIN)

    print(f"Sent signal SIGRTMIN to PID {pid} with payload: '{final_payload}'")

    shm.close()


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python sender.py <PID> <Hypercall ID> <Payload>")
        sys.exit(1)


def create_json(id: int, payload: Optional[str] = "{}") -> str:
    try:
        payload_dict = json.loads(payload)
    except:
        raise Exception(
            "Failed to read string as JSON! Try checking its formatting."
        )
    final_dict = {"id": id, "payload": payload_dict}
    return json.dumps(final_dict)


print(sys.argv)

if len(sys.argv) == 4:
    send_signal(int(sys.argv[1]), int(sys.argv[2]), sys.argv[3])
else:
    send_signal(int(sys.argv[1]), int(sys.argv[2]), "{}")
