#!/usr/bin/env python3
"""
Lightweight msgq-to-zmq bridge that only publishes whitelisted services.
Run this on the comma device instead of the full bridge to avoid
overloading the system when streaming to PlotJuggler.

PlotJuggler's "Cereal Subscriber" plugin connects to these ZMQ ports
using the same FNV1a port mapping as the C++ bridge.

Usage:
  ./zmq_pub_whitelist.py carControl carState
  ./zmq_pub_whitelist.py --decimation 5 carControl carState  # send every 5th msg
"""
import os
import ctypes
import ctypes.util

import cereal.messaging as messaging
from cereal.services import SERVICE_LIST

# load libzmq via ctypes since pyzmq may not be installed on device
_libzmq_path = ctypes.util.find_library("zmq")
assert _libzmq_path, "libzmq not found"
_libzmq = ctypes.CDLL(_libzmq_path)

ZMQ_PUB = 1
ZMQ_DONTWAIT = 1

# set up ctypes signatures
_libzmq.zmq_ctx_new.restype = ctypes.c_void_p
_libzmq.zmq_socket.restype = ctypes.c_void_p
_libzmq.zmq_socket.argtypes = [ctypes.c_void_p, ctypes.c_int]
_libzmq.zmq_bind.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
_libzmq.zmq_send.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_int]
_libzmq.zmq_close.argtypes = [ctypes.c_void_p]
_libzmq.zmq_ctx_term.argtypes = [ctypes.c_void_p]


def get_port(endpoint: str) -> int:
  """FNV1a hash to match the C++ bridge port mapping."""
  fnv_prime = 0x100000001b3
  hash_value = 0xcbf29ce484222325
  for c in endpoint.encode():
    hash_value ^= c
    hash_value = (hash_value * fnv_prime) & 0xFFFFFFFFFFFFFFFF
  start_port = 8023
  max_port = 65535
  return start_port + (hash_value % (max_port - start_port))


def zmq_pub_socket(ctx, endpoint: str):
  """Create and bind a ZMQ PUB socket on the hashed port for a service."""
  sock = _libzmq.zmq_socket(ctx, ZMQ_PUB)
  assert sock, f"Failed to create ZMQ socket for {endpoint}"
  port = get_port(endpoint)
  addr = f"tcp://*:{port}".encode()
  rc = _libzmq.zmq_bind(sock, addr)
  assert rc == 0, f"Failed to bind {endpoint} on port {port}"
  return sock, port


def main():
  import argparse
  parser = argparse.ArgumentParser(description="Lightweight msgq-to-zmq bridge for specific services")
  parser.add_argument("--decimation", type=int, default=5, help="Send every Nth message (default: 5, i.e. 20Hz)")
  parser.add_argument("services", nargs="+", help="Services to publish")
  args = parser.parse_args()

  for s in args.services:
    assert s in SERVICE_LIST, f"Unknown service: {s}"

  # Lower priority to avoid starving critical processes
  os.nice(10)

  ctx = _libzmq.zmq_ctx_new()
  assert ctx, "Failed to create ZMQ context"

  poller = messaging.Poller()
  sub_sockets = {}
  zmq_pubs = {}
  counters = {}

  for service in args.services:
    sub_sockets[service] = messaging.sub_sock(service, poller=poller, conflate=True)
    zmq_pubs[service], port = zmq_pub_socket(ctx, service)
    counters[service] = 0
    print(f"Publishing {service} on tcp://*:{port}")

  print(f"Streaming {len(args.services)} services (decimation={args.decimation}, nice=10). Ctrl+C to stop.")

  try:
    while True:
      for sock in poller.poll(100):
        msg = sock.receive(non_blocking=True)
        if msg is not None:
          for service, sub in sub_sockets.items():
            if sub is sock:
              counters[service] += 1
              if counters[service] >= args.decimation:
                _libzmq.zmq_send(zmq_pubs[service], msg, len(msg), ZMQ_DONTWAIT)
                counters[service] = 0
              break
  except KeyboardInterrupt:
    pass
  finally:
    for sock in zmq_pubs.values():
      _libzmq.zmq_close(sock)
    _libzmq.zmq_ctx_term(ctx)


if __name__ == "__main__":
  main()
