"""Builds the hall impulse responses the Octavio 2 engine ships (octavio2/data/halls/*.wav).

    python3 make_halls.py [IR folder]   # default: the final tune-up IR folder in project files

Each hall is a stereo pair at 48 kHz, as the final tune-up used it (finish.py HALLS): the
first capsule of the Arvedi receivers, both ears of the Detmold and BBC dummy heads, the
Cremona church's left and right omnis, the WDR front-facing binaural pair. The tail is cut
where its remaining energy is 60 dB below the whole response, with a 20 ms fade, so the
convolution does no work on silence. Sources and licences: SOURCES.md.
"""
import os, sys
import numpy as np, soundfile as sf

IRS = sys.argv[1] if len(sys.argv) > 1 else '/mnt/project-files/research/final-tune-up-render/irs/'
OUT = os.path.dirname(os.path.abspath(__file__))
FS = 48000
A = 'arvedi/arvedi_auditorium_dataset/rirs/rir-S0-'
HALLS = [
    ('arvedi-near', A + 'A206.wav', A + 'A209.wav'),
    ('arvedi-far', A + 'A505.wav', A + 'A510.wav'),
    ('detmold', 'detmold/SetA_SingleSources/Data/Brahmssaal/DummyHead/C1S1R3.wav', None),
    ('church', 'church/OMNI/SC_ML_OMNI_2.wav', 'church/OMNI/SC_MR_OMNI_2.wav'),
    ('maida-vale-4', 'bbc/MV4/AS4/AS4/KEMAR/MV4_AS4_KEMAR_R_OA-07_S_PA-13.wav', None),
    ('maida-vale-5', 'bbc/MV5/AS2/AS2/KEMAR/MV5_AS2_KEMAR_R_PA-3A_S_PA-7B_N.wav', None),
    ('wdr-control-room', 'wdr/cr1_front.wav', None),
    ('wdr-studio', 'wdr/sbs_front.wav', None),
]


def load(path):
    x, sr = sf.read(os.path.join(IRS, path), always_2d=True)
    assert sr == FS, (path, sr)
    return x


def main():
    for name, left, right in HALLS:
        if right is None:
            x = load(left)
            L, R = x[:, 0], x[:, 1]
        else:
            L, R = load(left)[:, 0], load(right)[:, 0]
        n = max(len(L), len(R))
        ir = np.stack([np.pad(L, (0, n - len(L))), np.pad(R, (0, n - len(R)))], 1)
        e = (ir ** 2).sum(1)[::-1].cumsum()[::-1]
        end = int(np.argmax(e < e[0] * 1e-6)) or n
        end = min(n, end + int(0.02 * FS))
        ir = ir[:end].copy()
        fade = int(0.02 * FS)
        ir[-fade:] *= np.linspace(1, 0, fade)[:, None]
        sf.write(os.path.join(OUT, name + '.wav'), ir.astype(np.float32), FS, subtype="PCM_24")
        print('%-18s %.2f s' % (name, end / FS))


if __name__ == '__main__':
    main()
