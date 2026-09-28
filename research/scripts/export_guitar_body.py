#!/usr/bin/env python3
"""The bowed acoustic guitar's body: resources/bodies/acoustic-guitar.wav.

Source: "Sweep guitare_dc" by pe_mace on Freesound (sound 696487, CC0), a
measured filter from a Godin guitar's under-saddle piezo pickup to a
microphone in front of it. The piezo reads the force the strings put on the
saddle, so this is the same bridge force -> radiated sound transfer as the
violin bodies (docs/BOWED_GUITAR.md).

The script downloads Freesound's high-quality preview (decoded with ffmpeg),
trims it to the violin bodies' 200 ms with a 20 ms fade, and writes a mono
32-bit float WAV at 48 kHz. Level is set in the plugin (Body::
measuredConvolutionRmsDb), not here.

    python3 research/scripts/export_guitar_body.py
"""

import pathlib
import struct
import subprocess
import tempfile
import urllib.request

import numpy as np

URL = "https://cdn.freesound.org/previews/696/696487_7061184-hq.mp3"
RATE = 48000
LENGTH = 0.2  # s, as the violin bodies
FADE = 0.02  # s
OUT = pathlib.Path(__file__).resolve().parents[2] / "resources" / "bodies" / "acoustic-guitar.wav"


def write_wav(path: pathlib.Path, samples: np.ndarray) -> None:
    data = samples.astype("<f4").tobytes()
    header = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE"
    header += b"fmt " + struct.pack("<IHHIIHH", 16, 3, 1, RATE, RATE * 4, 4, 32)
    header += b"data" + struct.pack("<I", len(data))
    path.write_bytes(header + data)


def main() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        mp3 = pathlib.Path(tmp) / "ir.mp3"
        urllib.request.urlretrieve(URL, mp3)
        raw = subprocess.run(
            ["ffmpeg", "-v", "error", "-i", str(mp3), "-ac", "1", "-ar", str(RATE), "-f", "f32le", "-"],
            check=True,
            capture_output=True,
        ).stdout
    ir = np.frombuffer(raw, dtype=np.float32).astype(np.float64)

    # Start one sample before the direct sound.
    start = max(0, int(np.argmax(np.abs(ir))) - 1)
    ir = ir[start : start + int(LENGTH * RATE)]
    fade = int(FADE * RATE)
    ir[-fade:] *= 0.5 * (1.0 + np.cos(np.linspace(0.0, np.pi, fade)))
    ir /= np.max(np.abs(ir))
    write_wav(OUT, ir)
    print(f"wrote {OUT} ({len(ir)} samples)")


if __name__ == "__main__":
    main()
