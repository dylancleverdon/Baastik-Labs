"""The Control Surface object Live talks to. All the work happens in engine.Engine."""

import Live

from .bridge import Bridge
from .engine import Engine

try:
    from _Framework.ControlSurface import ControlSurface as _FrameworkSurface
except Exception:  # pragma: no cover - Live without the legacy framework
    _FrameworkSurface = None


def _make_engine(song, log, show):
    return Engine(song, Live.Application.get_application(), Live, Bridge(log=log), log=log, show_message=show)


if _FrameworkSurface is not None:

    class TuttiSurface(_FrameworkSurface):
        def __init__(self, c_instance):
            _FrameworkSurface.__init__(self, c_instance)
            with self.component_guard():
                self._engine = _make_engine(self.song(), self.log_message, self.show_message)
            self.log_message("Tutti Remote Script loaded")

        def update_display(self):
            _FrameworkSurface.update_display(self)
            self._engine.tick()

        def disconnect(self):
            self._engine.shutdown()
            _FrameworkSurface.disconnect(self)

else:

    class TuttiSurface(object):
        """Minimal Control Surface for a Live without _Framework."""

        def __init__(self, c_instance):
            self._c = c_instance
            self._engine = _make_engine(c_instance.song(), self.log_message, self.show_message)

        def application(self):
            return Live.Application.get_application()

        def song(self):
            return self._c.song()

        def log_message(self, *message):
            self._c.log_message(" ".join(str(m) for m in message))

        def show_message(self, message):
            self._c.show_message(message)

        def update_display(self):
            self._engine.tick()

        def disconnect(self):
            self._engine.shutdown()

        def refresh_state(self):
            pass

        def build_midi_map(self, midi_map_handle):
            pass

        def receive_midi(self, midi_bytes):
            pass

        def connect_script_instances(self, instances):
            pass

        def can_lock_to_devices(self):
            return False

        def suggest_input_port(self):
            return ""

        def suggest_output_port(self):
            return ""

        def suggest_map_mode(self, cc_no, channel):
            return -1

        def port_settings_changed(self):
            pass
