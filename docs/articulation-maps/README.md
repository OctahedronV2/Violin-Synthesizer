# Octavio 2 articulation maps

Octavio 2 (2.2 and later) switches articulations with fixed keyswitches, C1 to B1 (MIDI 24-35,
with C4 = 60), whatever the Octave setting. There are two groups that combine:

- **Articulation** (MIDI 24-32): Arco, Pizzicato, Bartok pizz, Left-hand pizz, Harmonics,
  Tremolo, Sautille, Portato, Col legno battuto. The same as the Articulation parameter.
- **Contact point** (MIDI 33-35): Ordinario, Sul ponticello, Sul tasto. The same as the Contact
  Point parameter. Tremolo sul ponticello = F1 then A#1.

Keyswitches latch: one stays in force until another of its group arrives, or until you move the
matching parameter. The full table is in [keyswitches.md](keyswitches.md) (also as
[keyswitches.csv](keyswitches.csv)).

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
`octavio2/plugin/Parameters.h` and `.cpp` (`keyswitchFirst/Last`, `articulationNames`, `contactNames`).
