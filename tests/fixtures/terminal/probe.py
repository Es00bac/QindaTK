# SPDX-License-Identifier: LGPL-3.0-or-later
"""A raw PTY peer: announce readiness, collect input, persist exact bytes."""
import os
import select
import sys
import time
import tty

tty.setraw(0)
os.write(1, bytes.fromhex(sys.argv[1]) + b"READY")
data = bytearray()
deadline = time.monotonic() + 5
while len(data) < int(sys.argv[2]) and time.monotonic() < deadline:
    if select.select([0], [], [], 0.05)[0]:
        data.extend(os.read(0, 4096))
with open(sys.argv[3], "wb") as output:
    output.write(data)
