"""The Remote Script's connection to the Tutti app on this computer (127.0.0.1:47381).

Live calls us on its main thread about ten times a second, so everything here is non-blocking:
poll() connects when needed, reads what has arrived and writes what is queued."""

import errno
import select
import socket
import sys
import time

from . import framing

PORT = 47381


class Bridge(object):
    def __init__(self, port=PORT, log=None, clock=time.monotonic):
        self._port = port
        self._log = log or (lambda text: None)
        self._clock = clock
        self._sock = None
        self._decoder = None
        self._out = bytearray()
        self._next_attempt = 0.0
        self.connected = False

    def send(self, message):
        if self.connected:
            self._out.extend(framing.encode(message))

    def close(self):
        if self._sock is not None:
            try:
                self._sock.close()
            except Exception:
                pass
        self._sock = None
        self.connected = False
        self._out = bytearray()

    def poll(self):
        """Returns a list of events: ("connected",), ("message", dict), ("disconnected",)."""
        events = []
        if self._sock is None:
            now = self._clock()
            if now < self._next_attempt:
                return events
            self._next_attempt = now + 2.0
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(0.05)
                sock.connect(("127.0.0.1", self._port))
                sock.setblocking(False)
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                if sys.platform == "darwin":
                    # Writing to a closed Tutti app must never raise SIGPIPE inside Live.
                    try:
                        sock.setsockopt(socket.SOL_SOCKET, getattr(socket, "SO_NOSIGPIPE", 0x1022), 1)
                    except (OSError, socket.error):
                        pass
            except (OSError, socket.error):
                try:
                    sock.close()
                except Exception:
                    pass
                return events
            self._sock = sock
            self._decoder = framing.Decoder()
            self._out = bytearray()
            self.connected = True
            events.append(("connected",))

        try:
            while True:
                readable, _, _ = select.select([self._sock], [], [], 0)
                if not readable:
                    break
                chunk = self._sock.recv(1 << 16)
                if not chunk:
                    raise OSError("closed")
                self._decoder.feed(chunk)
            while True:
                message = self._decoder.next()
                if message is None:
                    break
                events.append(("message", message))
            if self._decoder.failed:
                raise OSError("bad frame")
            self.flush()
        except (OSError, socket.error, ValueError) as e:
            self._log("Tutti app connection closed (%s)" % e)
            self.close()
            events.append(("disconnected",))
        return events

    def flush(self):
        while self._out and self._sock is not None:
            try:
                sent = self._sock.send(bytes(self._out[:1 << 16]))
            except (OSError, socket.error) as e:
                if getattr(e, "errno", None) in (errno.EAGAIN, errno.EWOULDBLOCK):
                    return
                raise
            if sent <= 0:
                return
            del self._out[:sent]
