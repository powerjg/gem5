#!/usr/bin/env python3

import json
import os
import signal
import sys
from multiprocessing import shared_memory
from time import sleep
from typing import Optional


def send_signal(pid: int, id: int, payload: str) -> None:
    shared_mem_name = "shared_gem5_signal_mem_" + str(pid)
    shared_mem_size = 4096
    try:
        shm = shared_memory.SharedMemory(
            name=shared_mem_name, create=True, size=shared_mem_size
        )
    except FileExistsError:
        shm = shared_memory.SharedMemory(name=shared_mem_name)

    shm.buf[:shared_mem_size] = b"\x00" * shared_mem_size
    final_payload = create_json(id, payload)
    shm.buf[: len(final_payload.encode())] = final_payload.encode()
    try:
        os.kill(pid, signal.SIGRTMIN)
    except ProcessLookupError:
        print(
            "Process does not exist! Check that you are using the correct PID."
        )
        shm.close()
        shm.unlink()
        sys.exit(1)

    print(f"Sent signal SIGRTMIN to PID {pid} with payload: '{final_payload}'")

    while bytes(shm.buf[:shared_mem_size]).decode().strip("\x00") != "done":
        print("Waiting for gem5 to finish using shared memory...")
        sleep(1)
    print("Done message received")
    shm.close()
    try:
        shm.unlink()
    except FileNotFoundError:
        pass


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
