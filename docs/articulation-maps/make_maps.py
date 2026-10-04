# Writes Octavio 2's DAW articulation maps next to this script: python3 make_maps.py
import os
from xml.sax.saxutils import escape

out = os.path.dirname(os.path.abspath(__file__))
os.makedirs(out, exist_ok=True)

NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
def name(n, c4=60):  # C4 = 60 (Reaper, Logic, Cubase default, FL Studio shows C5 for 60)
    return NAMES[n % 12] + str(n // 12 - 1)

# (key, name, description, group, Reaticulate colour, Reaticulate icon, Cubase type 0 attribute 1 direction)
ARTS = [
    (24, 'Arco', 'bowed (normal)', 1, 'long', 'note-whole'),
    (25, 'Pizzicato', 'plucked', 1, 'short-light', 'pizz'),
    (26, 'Bartok pizz', 'snap pizzicato', 1, 'short-dark', 'pizz-bartok'),
    (27, 'Left-hand pizz', 'plucked by a left-hand finger', 1, 'short-light', 'pizz-mute'),
    (28, 'Harmonics', 'natural or artificial harmonics', 1, 'fx', 'harmonics'),
    (29, 'Tremolo', 'quick strokes in one note', 1, 'textured', 'tremolo'),
    (30, 'Sautille', 'bouncing strokes, mid-bow', 1, 'short', 'spiccato'),
    (31, 'Portato', 'pulses in one bow', 1, 'long-light', 'portato'),
    (32, 'Col legno battuto', 'the stick strikes the string', 1, 'fx', 'col-legno'),
    (33, 'Ordinario', 'contact point as dynamics place it', 2, 'default', 'note-whole'),
    (34, 'Sul ponticello', 'bowed at the bridge', 2, 'textured', 'sul-pont'),
    (35, 'Sul tasto', 'bowed over the fingerboard', 2, 'long-dark', 'sul-tasto'),
]

# ------------------------------------------------------------- plain table (FL Studio and any DAW)
rows = ['| MIDI note | Name (C4 = 60) | FL Studio name (C5 = 60) | Switches to | Group |', '|---|---|---|---|---|']
for k, n, d, g, _, _ in ARTS:
    rows.append(f'| {k} | {name(k)} | {NAMES[k % 12]}{k // 12} | {n}: {d} | {"articulation" if g == 1 else "contact point"} |')
table = '\n'.join(rows)
open(f'{out}/keyswitches.md', 'w').write(
    '# Octavio 2 keyswitches\n\n'
    'Fixed keys, whatever the Octave setting. Latching: a keyswitch stays in force until another one in its group, '
    'or until the Articulation / Contact Point parameter is changed. The two groups combine (tremolo sul ponticello: '
    'F1 then A#1).\n\n' + table + '\n')
with open(f'{out}/keyswitches.csv', 'w') as f:
    f.write('note,name_c4_60,name_fl_studio,articulation,group\n')
    for k, n, d, g, _, _ in ARTS:
        f.write(f'{k},{name(k)},{NAMES[k % 12]}{k // 12},{n},{"articulation" if g == 1 else "contact"}\n')

# ------------------------------------------------------------- Reaper: Reaticulate bank
lines = ['//! g="Octavio" n="Octavio 2 violin"',
         '//! m="Octavio 2 (2.2): keyswitches C1-B1. Group 1 articulation, group 2 contact point."',
         'Bank 72 2 Octavio 2 violin']
for i, (k, n, d, g, col, icon) in enumerate(ARTS):
    prog = i if g == 1 else 20 + (i - 9)
    grp = '' if g == 1 else ' g=2'
    lines.append(f'//! c={col} i={icon}{grp} o=note:{k}')
    lines.append(f'{prog} {n}')
open(f'{out}/Octavio2.reabank', 'w').write('\n'.join(lines) + '\n')

# ------------------------------------------------------------- Cubase / Nuendo expression map
ids = iter(range(100001, 200000))
def visual(n, d, g):
    return (f'<obj class="USlotVisuals" ID="{{id}}">\n'
            f'   <int name="displaytype" value="1"/>\n'
            f'   <int name="articulationtype" value="{1 if g == 1 else 0}"/>\n'
            f'   <int name="symbol" value="73"/>\n'
            f'   <string name="text" value="{escape(n)}" wide="true"/>\n'
            f'   <string name="description" value="{escape(d)}" wide="true"/>\n'
            f'   <int name="group" value="{g - 1}"/>\n'
            f'</obj>')
def indent(s, n):
    return '\n'.join(' ' * n + l for l in s.split('\n'))
vis = []
for k, n, d, g, _, _ in ARTS:
    vid = next(ids)
    vis.append((vid, visual(n, d, g).replace('{id}', str(vid))))
slots = []
for (k, n, d, g, _, _), (vid, v) in zip(ARTS, vis):
    slots.append(f'''<obj class="PSoundSlot" ID="{next(ids)}">
   <obj class="PSlotThruTrigger" name="remote" ID="{next(ids)}">
      <int name="status" value="144"/>
      <int name="data1" value="-1"/>
   </obj>
   <obj class="PSlotMidiAction" name="action" ID="{next(ids)}">
      <int name="version" value="600"/>
      <member name="noteChanger">
         <int name="ownership" value="1"/>
         <list name="obj" type="obj">
            <obj class="PSlotNoteChanger" ID="{next(ids)}">
               <int name="channel" value="-1"/>
               <float name="velocityFact" value="1"/>
               <float name="lengthFact" value="1"/>
               <int name="minVelocity" value="0"/>
               <int name="maxVelocity" value="127"/>
               <int name="transpose" value="0"/>
               <int name="minPitch" value="0"/>
               <int name="maxPitch" value="127"/>
            </obj>
         </list>
      </member>
      <member name="midiMessages">
         <int name="ownership" value="1"/>
         <list name="obj" type="obj">
            <obj class="POutputEvent" ID="{next(ids)}">
               <int name="status" value="144"/>
               <int name="data1" value="{k}"/>
               <int name="data2" value="100"/>
               <int name="data3" value="0"/>
            </obj>
         </list>
      </member>
      <int name="channel" value="-1"/>
      <float name="velocityFact" value="1"/>
      <float name="lengthFact" value="1"/>
      <int name="minVelocity" value="0"/>
      <int name="maxVelocity" value="127"/>
      <int name="transpose" value="0"/>
      <int name="maxPitch" value="127"/>
      <int name="minPitch" value="0"/>
      <int name="key" value="-1"/>
   </obj>
   <member name="sv">
      <int name="ownership" value="2"/>
      <list name="obj" type="obj">
{indent(v, 9)}
      </list>
   </member>
   <member name="name">
      <string name="s" value="{escape(n)}" wide="true"/>
   </member>
   <int name="color" value="{0 if g == 1 else 2}"/>
</obj>''')
xml = f'''<?xml version="1.0" encoding="utf-8"?>
<InstrumentMap>
   <string name="name" value="Octavio 2 violin" wide="true"/>
   <member name="controller">
      <int name="ownership" value="1"/>
   </member>
   <member name="slots">
      <int name="ownership" value="1"/>
      <list name="obj" type="obj">
{indent(chr(10).join(slots), 9)}
      </list>
   </member>
   <member name="slotvisuals">
      <int name="ownership" value="1"/>
      <list name="obj" type="obj">
{indent(chr(10).join(v for _, v in vis), 9)}
      </list>
   </member>
   <string name="name" value="Octavio 2 violin" wide="true"/>
</InstrumentMap>
'''
open(f'{out}/Octavio2.expressionmap', 'w').write(xml)

# ------------------------------------------------------------- Logic Pro articulation set
arts = []
for i, (k, n, d, g, _, _) in enumerate(ARTS):
    arts.append(f'''		<dict>
			<key>ArticulationID</key>
			<integer>{i + 1}</integer>
			<key>ID</key>
			<integer>{1001 + i}</integer>
			<key>Name</key>
			<string>{escape(n)}</string>
			<key>Output</key>
			<array>
				<dict>
					<key>MB1</key>
					<integer>{k}</integer>
					<key>Status</key>
					<string>Note On</string>
					<key>ValueLow</key>
					<integer>100</integer>
				</dict>
			</array>
		</dict>''')
plist = f'''<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>Articulations</key>
	<array>
{chr(10).join(arts)}
	</array>
	<key>Name</key>
	<string>Octavio 2 violin</string>
</dict>
</plist>
'''
open(f'{out}/Octavio2.plist', 'w').write(plist)

