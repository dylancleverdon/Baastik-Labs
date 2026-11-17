"""Length-prefixed JSON frames, identical to core/src/Protocol.cpp:
[4-byte big-endian length of the rest]['J'][UTF-8 JSON]."""

import json
import struct

MAX_FRAME = 64 * 1024 * 1024


def encode(message):
    data = json.dumps(message, separators=(",", ":")).encode("utf-8")
    return struct.pack(">I", len(data) + 1) + b"J" + data


class Decoder(object):
    def __init__(self):
        self._buffer = bytearray()
        self.failed = False

    def feed(self, data):
        if not self.failed:
            self._buffer.extend(data)

    def next(self):
        if self.failed or len(self._buffer) < 4:
            return None
        (length,) = struct.unpack(">I", bytes(self._buffer[:4]))
        if length < 1 or length > MAX_FRAME:
            self.failed = True
            return None
        if len(self._buffer) < 4 + length:
            return None
        kind = self._buffer[4:5]
        payload = bytes(self._buffer[5:4 + length])
        del self._buffer[:4 + length]
        if kind != b"J":
            return {"type": "_binary"}  # the Remote Script never needs binary frames: skip them
        try:
            return json.loads(payload.decode("utf-8"))
        except ValueError:
            self.failed = True
            return None
