# Octavio 2 articulation maps

Octavio 2 (2.2 and later) switches articulations with keyswitches, C1 to B1 (MIDI 24-35, with
C4 = 60) by default, whatever the Octave setting. There are two groups that combine:

- **Articulation** (MIDI 24-32): Arco, Pizzicato, Bartok pizz, Left-hand pizz, Harmonics,
  Tremolo, Sautille, Portato, Col legno battuto. The same as the Articulation parameter.
- **Contact point** (MIDI 33-35): Ordinario, Sul ponticello, Sul tasto. The same as the Contact
  Point parameter. Tremolo sul ponticello = F1 then A#1.

Keyswitches latch: one stays in force until another of its group arrives, or until you move the
matching parameter. The full table is in [keyswitches.md](keyswitches.md) (also as
[keyswitches.csv](keyswitches.csv)).

## Keyswitch Start and Behaviour (2.3)

Both are on the MIDI tab and are automatable parameters (`keyswitchStart`, `keyswitchMode`):

- **Keyswitch Start** (default C1 = MIDI 24) moves the whole block of twelve keys: the first nine
  keys from it are the articulations in the order above, the last three the contact points. With
  Start at C2 (36), C#2 is pizzicato and A#2 sul ponticello. The maps in this folder are written
  for the default; for another Start, transpose their keys by the same amount.
- **Keyswitch Behaviour**:
  - *Latching* (default, as in 2.2): a keyswitch stays in force until the next one.
  - *Momentary*: the articulation plays only while the key is held; letting go returns to what
    played before (the parameter, or the last latched keyswitch).
  - *Off*: the keys are not keyswitches; they play as ordinary notes (silent below the violin's
    G3), so the block can overlap notes you want to play.

The Play tab's articulation strip (and the Articulation tab) light up what a keyswitch or UACC
chose; clicking a choice goes back to the parameter.

## UACC (CC32)

Octavio 2.3 also follows UACC (Universal Articulation Controller Codes, the CC32 convention from
Spitfire Audio): a CC32 value latches an articulation, a bow style and a contact point together,
as a keyswitch does (moving the Articulation, Bow Style or Contact parameter wins again). Only the
values with a clear meaning on a solo violin are used; the others (and 0) change nothing.

| CC32 | UACC | Octavio 2 |
|---|---|---|
| 1-7, 9 | Long | Arco, Auto bowing, ordinario |
| 8 | Long soft (flautando) | Arco, sul tasto |
| 10 | Long harmonics | Harmonics |
| 11, 12, 15 | Tremolo | Tremolo |
| 13 | Tremolo soft (sul tasto) | Tremolo, sul tasto |
| 14 | Tremolo hard (sul ponticello) | Tremolo, sul ponticello |
| 20-29 | Legato | Arco, Legato bow style |
| 40 | Short | Arco, Staccato |
| 41 | Short alternative | Arco, Martelé |
| 42 | Very short | Arco, Spiccato |
| 43 | Very short soft | Sautillé |
| 44 | Short leisurely | Arco, Détaché |
| 50 | (Octavio) | Portato |
| 56 | Pizzicato | Pizzicato |
| 57 | (Octavio) | Left-hand pizz |
| 58 | Pizzicato Bartók | Bartók pizz |
| 59, 60 | Col legno | Col legno battuto |

In Live mode, where a note's length is not known when it starts, Staccato, Martelé and Spiccato
play strokes of about 120, 150 and 90 ms (in Studio mode and from a score: a share of the note).

## The map files

| File | DAW | How to load |
|---|---|---|
| `Octavio2.reabank` | Reaper with [Reaticulate](https://reaticulate.com) | Reaticulate: Settings > Edit user banks, paste the file's contents (or add it as a bank file), then pick "Octavio 2 violin" for the track. Group 2 holds the contact points. |
| `Octavio2.expressionmap` | Cubase, Nuendo | Expression Map Setup > Import, then choose the map in the track's inspector. Contact points are directions in group 2, so they combine with an articulation. |
| `Octavio2.plist` | Logic Pro (10.5+) | Track inspector > Articulation Set > Load. |
| `keyswitches.md` / `.csv` | FL Studio and any other DAW | Place the keyswitch notes a little before the notes they change (FL Studio names MIDI 24 "C2"; Octavio 2's own keyboard shows the keyswitch keys in blue). |

Not included:

- **Studio One Sound Variations**: Studio One's own variation file format is not documented, so
  we did not guess it. Build the set in Studio One's Sound Variations editor from the table (each
  variation: a Note activation on the listed key), and save it as a preset.
- **Dorico**: Dorico expression maps (.doricolib) are not included; use the table.

The Reaticulate bank follows Reaticulate's documented bank format. The Cubase and Logic files were
written to those programs' file layouts but could not be tested in the programs themselves while
building them: if one does not import, please tell us (and use the table meanwhile).

The files are written by `make_maps.py` (run it after the keyswitches change); the plugin side is in
`octavio2/plugin/Parameters.h` and `.cpp` (`keyswitchFirst`, `keyswitchCount`, `articulationNames`,
`contactNames`) and `octavio2/core/Player.h` (`uaccMap`).
