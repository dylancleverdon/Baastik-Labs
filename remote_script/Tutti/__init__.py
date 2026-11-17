"""Tutti: edit one Ableton Live Set together, live.

Live loads this as a Control Surface (Settings > Link, Tempo & MIDI > Control Surface: Tutti).
It talks only to the Tutti app on this computer; the app does the networking."""


def create_instance(c_instance):
    from .surface import TuttiSurface
    return TuttiSurface(c_instance)
