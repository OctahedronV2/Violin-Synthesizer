# Render the listening clips: bridge force from the lab -> Iowa body IR -> -20 dB RMS -> mp3.
import subprocess, os, numpy as np, soundfile as sf
from scipy.signal import fftconvolve
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))); R = ROOT + '/results'
OUT = '/mnt/project-files/showcase/string-physics'; os.makedirs(OUT, exist_ok=True)
LAB = os.environ.get('LAB', '/tmp/lab3')
body, _ = sf.read('/home/user/Violin-Synthesizer/resources/bodies/iowa.wav')
TH = ['friction=thermal', 'muS=1.05', 'autoTune=1']; HR = ['hairStiffness=110000', 'hairDamping=10', 'bowPoints=4']
CFG = {
    '1-lite-today': ['perString=0'],
    '2-thermal-rosin-real-hair': ['strings=' + R + '/strings_pickering.txt'] + TH + HR,
    '3-plus-bridge-and-sympathetic-strings': ['strings=' + R + '/strings_bridge0.5.txt', 'admittance=1', 'modes=' + R + '/modes_generic.txt', 'admScale=0.5'] + TH + HR,
}
def finish(wav, name):
    x, sr = sf.read(wav); y = fftconvolve(x, body)[:len(x)+len(body)]
    y = y/np.sqrt(np.mean(y[:int(len(y)*0.8)]**2))*10**(-20/20)
    y = np.clip(y, -0.99, 0.99)
    sf.write('/tmp/clip.wav', y.astype(np.float32), sr)
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', '/tmp/clip.wav', '-b:a', '192k', f'{OUT}/{name}.mp3'], check=True)
for k, o in CFG.items():
    subprocess.run([LAB, 'score', ROOT + '/clips/phrase.txt', '/tmp/c.wav'] + o, check=True); finish('/tmp/c.wav', f'phrase-{k}')
    subprocess.run([LAB, 'note', 'A', '69', '0.03', '0.2', '0.1', '9', '/tmp/c.wav', 'fend=3', 'ring=1'] + o, check=True, capture_output=True); finish('/tmp/c.wav', f'pressure-sweep-{k}')
# real reference: the four open strings, mf, from the Iowa recordings (first note of each file)
import sys; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from seg import notes
parts = []
for s, f in [('G', 'Violin.arco.mf.sulG.G3B3.wav'), ('D', 'Violin.arco.mf.sulD.D4A4.wav'), ('A', 'Violin.arco.mf.sulA.A4B4.wav'), ('E', 'Violin.arco.mf.sulE.E5B5.wav')]:
    x, sr, segs = notes(ROOT + '/refs/iowa/' + f); a, b = next((a, b) for a, b in segs if (b-a)/sr > 2)
    parts.append(x[a:a+int(2.5*sr)]); parts.append(np.zeros(int(0.3*sr)))
y = np.concatenate(parts); y = y/np.sqrt(np.mean(y**2))*10**(-20/20)
sf.write('/tmp/clip.wav', y.astype(np.float32), 48000)
subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', '/tmp/clip.wav', '-b:a', '192k', f'{OUT}/real-iowa-open-strings-mf.mp3'], check=True)
# the same four open strings from each model (2.5 s notes, mf bow)
for k, o in CFG.items():
    parts = []
    for s, m, F in [('G', 55, 0.8), ('D', 62, 0.7), ('A', 69, 0.5), ('E', 76, 0.45)]:
        subprocess.run([LAB, 'note', s, str(m), str(F), '0.2', '0.1', '2.5', '/tmp/n.wav', 'ring=0.3'] + o, check=True, capture_output=True)
        x, sr = sf.read('/tmp/n.wav'); parts.append(x)
    sf.write('/tmp/c.wav', np.concatenate(parts).astype(np.float32), 48000); finish('/tmp/c.wav', f'open-strings-{k}')
print(sorted(os.listdir(OUT)))
