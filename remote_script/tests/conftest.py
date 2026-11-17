import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "fakelive"))
sys.path.insert(0, os.path.dirname(HERE))

import pytest  # noqa: E402

import Live  # noqa: E402  (the fake)


class FakeBridge(object):
    """Stands in for the socket to the Tutti app."""

    def __init__(self):
        self.connected = True
        self.inbox = []
        self.sent = []
        self._events = [("connected",)]

    def poll(self):
        events = self._events + [("message", m) for m in self.inbox]
        self._events = []
        self.inbox = []
        return events

    def send(self, message):
        self.sent.append(message)

    def flush(self):
        pass

    def close(self):
        self.connected = False

    def take(self, kind):
        found = [m for m in self.sent if m.get("type") == kind]
        self.sent = [m for m in self.sent if m.get("type") != kind]
        return found

    def ops(self):
        out = []
        for m in self.take("ops"):
            out.extend(m["ops"])
        return out


class Clock(object):
    def __init__(self):
        self.t = 1000.0

    def __call__(self):
        return self.t


@pytest.fixture(autouse=True)
def fresh_features():
    Live.reset_features()
    yield
    Live.reset_features()


def make_engine(song=None):
    from Tutti.engine import Engine
    song = song or Live.Song.Song()
    app = Live.Application.Application(song)
    bridge = FakeBridge()
    clock = Clock()
    engine = Engine(song, app, Live, bridge, clock=clock)
    engine.tick()  # "connected" -> hello
    bridge.take("hello")
    return engine, song, bridge, clock
