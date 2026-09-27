# Phase 4: Four Strings, Allocation and Expression

The plugin now models the whole instrument rather than a single voice:
- four independent bowed strings, which make double, triple and quadruple stops possible;
- one shared bow, which turns on each new stroke and changes automatically when it runs out;
- sympathetic resonance of the open strings;
- humanised vibrato;
- MPE.

## What changed

| Part | File | What it does |
|---|---|---|
| String allocation | `source/engine/StringAllocator.*` | Decides which string plays each note (rules below) |
| String voice | `source/engine/StringVoice.*` | One per string: bow envelope, glide, bends, humanised vibrato, bow position from the setting or per-note CC74 |
| Violin | `source/engine/Violin.*` | Four string voices, the allocator, the bow and MIDI/MPE handling |
| Sympathetic resonance | `source/engine/SympatheticStrings.*` | Undamped open strings, driven through the bridge (below) |
| Parameters and editor | `source/plugin/*` | New **Play** section. The editor now has two rows. |

### How notes are assigned to strings

A string plays one note at a time, and only notes at or above its open pitch.

- **Chords:** notes starting within 40 ms of each other become a double, triple or quadruple stop. The highest note goes on the highest string that can play it, and chord notes are re-spread if needed. For example, C5 then D5 puts D5 on the A string and moves C5 to the D string. When a chord is impossible, such as two notes that both need the G string, the newest note wins.
- **Legato lines:** a note that overlaps the previous one slurs without a new bow stroke. It stays on the same string for steps up to a fifth within the string's first octave; otherwise it crosses to the usual string mid-bow. Releasing the newest note of a trill returns to the note still held. A melody played over a held double stop keeps the chord sounding.
- **Play Mode:**
  - **Auto** (default) follows the chord and legato rules above.
  - **Mono legato** plays one line only; overlapping notes always slur.
  - **Poly** gives every note its own stroke on its own string.

### The bow

There is one bow. Each new stroke turns it, and the notes of a chord share a stroke. The 62 cm of bow hair gets used up as it moves. When it runs out, an **automatic bow change** follows: the bow slows over 80 ms, turns at its slowest point, and speeds up again, as a player does on a long note. This can be switched off with **Auto bow change**.

### Sympathetic resonance

Open strings that aren't being played ring along with notes whose harmonics match them. A5, for example, sets the open A string ringing through its 2nd harmonic.

**Resonance** sets the amount (default 30%). It is modelled as a one-way coupling at the host rate: the fingered strings' bridge force drives linear open-string resonators. These use the same waveguide, without friction. The resonators never drive anything back.

The first version was two-way and ran every string at 192 kHz, and a test caught it growing without bound. With all four strings ringing at full resonance, the level rose from 0.23 to 0.84 RMS after the notes were released. Strings tuned in fifths share harmonics, and the coupling let energy circulate between them. The one-way design is stable by construction, and it cut CPU from 7.5% to about 4% of a core.

### Expression

| Input | Without MPE | With MPE on |
|---|---|---|
| Velocity | Dynamics (bow speed) of the note | Same |
| CC11 / CC2 | Dynamics of all strings, once received | Same |
| CC1 | Bow pressure, once received | Same |
| CC74 | Bow position of all strings (tasto → ponticello), once received | Per note, on its member channel |
| Aftertouch | Extra vibrato depth, up to 30 cents, all strings | Per note |
| Pitch bend | All strings, ± **Bend** range | Member channels bend their own note by ± **MPE Bend** (default 48); channel 1 bends everything by ± **Bend** |

The MPE lower zone is assumed: channel 1 is the master and channels 2–16 are member channels.

**Humanise** (default 50%) lets the vibrato rate drift by up to ±8% and the depth by up to ±25%. The drift is slow, independent random wandering per string.

## Verification

- **Allocator unit tests** (14): string choice, double and quadruple stops, chord re-spreading, impossible chords, legato on a string and across strings, trills, a melody over a held chord, Mono legato and Poly modes, MPE channels, and all-notes-off.
- **Engine tests:**
  - A double stop sounds both pitches, each more than 20 dB above the background.
  - A legato leap arrives on the E string.
  - Sympathetic resonance raises the level of the open A string's partials in the tail after A5 by more than 1.5×.
  - Maximum resonance always decays, both with four ringing strings and with one loud note driving three open strings.
  - With MPE, a member channel's bend moves only its own note, and the master channel moves both.
  - An 8 s note gets at least five automatic bow changes, and none when the feature is off.
  - Random MIDI in every play mode, with MPE, pressure, CC74 and bends, stays finite and bounded.
- **Sanitisers:** clean under AddressSanitizer and UBSan.
- **pluginval:** strictness 10 passes.
- **Demo render:** the double-stop phrase shows every chord note present, 25–60 dB above the background.

### Level and CPU

- **Level:** the single-note calibration is now −21 dBFS RMS (it was −18) to leave headroom for chords. A loud four-string chord peaks at about −2 dBFS.
- **CPU:** an mf A4 with 30% resonance uses 3.7–4.4% of one core at 48 kHz. Each extra sounding string adds about 2%.

## Not in this phase

- **Presets** belong to Phase 6 in the plan, together with the UI work.
- **Articulations** (Phase 5): pizzicato, spiccato, tremolo, harmonics and keyswitches.
- **MPE Configuration Messages** are not parsed. The lower-zone layout above is assumed.
