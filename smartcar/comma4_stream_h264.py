#!/usr/bin/env python3
"""Runs on the comma4. Dumps raw H264 NAL units to stdout, starting from a keyframe."""
import sys
import os
import time

os.environ['PYTHONPATH'] = '/data/openpilot'
sys.path.insert(0, '/data/openpilot')

import cereal.messaging as messaging

service = sys.argv[1] if len(sys.argv) > 1 else 'livestreamWideRoadEncodeData'

sock = messaging.sub_sock(service, conflate=True)
time.sleep(0.5)

got_keyframe = False

while True:
    msg = messaging.recv_one(sock)
    if msg is None:
        continue
    evt = getattr(msg, msg.which())

    if not got_keyframe:
        if len(evt.header) > 0:
            got_keyframe = True
            sys.stdout.buffer.write(evt.header)
        else:
            continue

    sys.stdout.buffer.write(evt.data)
    sys.stdout.buffer.flush()
