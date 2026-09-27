"""Physical data for the four violin strings.

Tensions are typical values for a synthetic-core set with a steel E, and the
vibrating length is the standard 328 mm. The linear density and wave impedance
follow from f0 = sqrt(T / rho) / (2 L) and Z = sqrt(T * rho).
"""

from __future__ import annotations

import math
from dataclasses import dataclass

VIBRATING_LENGTH_M = 0.328


@dataclass(frozen=True)
class StringSpec:
    name: str
    midi_note: int
    tension_n: float
    length_m: float = VIBRATING_LENGTH_M

    @property
    def open_f0(self) -> float:
        return 440.0 * 2.0 ** ((self.midi_note - 69) / 12.0)

    @property
    def linear_density(self) -> float:
        """kg/m"""
        return self.tension_n / (2.0 * self.length_m * self.open_f0) ** 2

    @property
    def impedance(self) -> float:
        """Transverse wave impedance Z = sqrt(T * rho), in kg/s."""
        return math.sqrt(self.tension_n * self.linear_density)

    @property
    def wave_speed(self) -> float:
        return math.sqrt(self.tension_n / self.linear_density)


STRINGS: dict[str, StringSpec] = {
    "G": StringSpec("G", 55, 45.0),
    "D": StringSpec("D", 62, 45.0),
    "A": StringSpec("A", 69, 57.0),
    "E": StringSpec("E", 76, 78.0),
}


def midi_to_hz(note: float) -> float:
    return 440.0 * 2.0 ** ((note - 69.0) / 12.0)


def string_for_note(midi_note: float) -> StringSpec:
    """Highest string whose open pitch is at or below the note."""
    candidates = [s for s in STRINGS.values() if s.midi_note <= midi_note]
    return max(candidates, key=lambda s: s.midi_note) if candidates else STRINGS["G"]
