# Phase 6: Presets and a New Editor

This phase adds 27 factory presets that load with one click, user presets, and a redesigned, resizable editor.

## Presets

### Using them

- The **preset bar** at the top of the editor shows the current preset. An asterisk (*) after the name means you've changed something since loading it.
  - **<** and **>** step through all presets.
  - Clicking the name opens a menu of every preset by category, your own presets, and **Save preset…**, **Show preset folder** and **Rescan presets**.
  - Hover over the name to see the preset's description.
- **Save** stores the current settings as a user preset, with a name and a category (default "User"). Saving under an existing name overwrites that preset. **Delete** removes the current user preset; factory presets can't be deleted.
- User presets are small XML files (`.vspreset`) in `Documents/OctahedronV2/Violin Synthesizer/Presets`. Copy them between computers or share them.
- **In the DAW:** the factory presets are also the plugin's *programs*, so they appear in the host's own preset list. In FL Studio, that is the plugin wrapper's preset menu.
- **Saving with the project:** the current preset name and any edits are saved in the host project and come back when it is reopened.

A preset always sets every parameter. Anything a preset doesn't mention goes back to its default, so the same preset always sounds the same whatever was loaded before.

### Factory presets

All presets are **level-matched**: each has an output-gain correction, so switching presets doesn't make you jump. On a test phrase, the loudest 100 ms of each preset is within 0.7 dB of *Default Violin* with the light body. A test enforces ±1.5 dB. With the measured bodies, all are within +1.5/−3.5 dB.

| Category | Preset | Character |
|---|---|---|
| Solo | Default Violin | The plugin's default: balanced, small room |
| | Romantic Soloist | Wide, early vibrato, slides, generous hall |
| | Intimate Close-Mic | Close and dry |
| | Bright Soloist | Bowed nearer the bridge, firmer: projecting |
| | Dark & Warm | Towards the fingerboard on the darker Iowa violin, slower vibrato |
| | Singing Legato | Mono legato line with audible shifts |
| Styles | Baroque | Almost no vibrato, light bow, ringing open strings, church acoustic |
| | Folk Fiddle | Quick gritty bowing, little vibrato, droning open strings |
| | Gypsy Swing | Fast, wide vibrato from the start, slides |
| | Fiddle Double Stops | Poly mode: every note its own string, sharp attack |
| | Film Lyrical | Slow swelling attacks, large wide hall |
| Articulations | Detache, Staccato, Spiccato, Tremolo, Pizzicato, Pizzicato Hall, Harmonics, Sul Ponticello, Sul Tasto, Con Sordino | Each Phase 5 articulation, with suitable vibrato and room |
| Character | Practice Mute | Heavy mute, small dry room |
| | Eerie Tremolo | Light tremolo near the bridge in a vast space |
| | Dry Studio | No room, narrow image: bring your own reverb |
| | Slow Swells | Long attacks and releases for pads |
| Expressive | MPE Expressive | MPE on: per-note bend, pressure → vibrato, slide → bow position |
| | Mono Lead | Monophonic lead with quick natural shifts |

Keyswitches still work on top of any preset. They change the articulation until the next keyswitch or preset change.

## The editor

![Editor](editor.png)

- **Resizable:** drag the corner from 70% to 200% of the 1100 × 700 base size. The layout is drawn once at the base size and scaled as a whole, so it stays sharp and in proportion. The aspect ratio is fixed.
- **Custom look:** knobs, buttons, menus and dialogs are drawn as vectors in a single warm palette.
- **Bow pad:** one pad controls bow position (left: bridge, right: fingerboard) and pressure (up: firmer). Drag it, use the arrow keys (hold Shift for fine steps), or double-click to reset. The Position and Pressure knobs next to it show the same two parameters.
- **Strings:** the four strings, drawn from nut (left) to bridge (right).
  - A dot marks where the finger stops the string for the note being played, using the real finger position, 1 − 2^(−semitones/12) of the string length.
  - The string glows as it sounds, and a meter shows its level.
- **Articulation:** ten buttons select the articulation, and the one playing now is highlighted, including after a keyswitch. Hovering over a button shows its keyswitch note.
- **Pitch, Play, Body & Output:** the same controls as before, regrouped. **Output Gain** now sits with Body.
- **Keyboard:** the on-screen keyboard labels middle C as C4, matching the note names elsewhere in the editor.
- **Accessibility:** every knob, menu, button and toggle has an accessible title, which a test checks. The bow pad describes its current position and pressure, and the string display describes the notes playing. Everything can be reached by keyboard.

## Verification

- **Presets** (`tests/PresetTests.cpp`, 6 cases):
  - The factory presets are unique, fully valid and in range, and the first is the default sound.
  - Loading a preset sets every parameter, and the modified flag works, as do previous and next.
  - User presets save, load, overwrite and delete, including names with characters that can't go in a file name.
  - Host programs map to the factory presets.
  - The current preset survives a project save and reload, including edits on top.
  - The presets are level-matched.
- **Editor:** it scales in proportion, and every control has an accessible title.
- All 63 test cases pass in Release and under AddressSanitizer + UBSan. pluginval passes at strictness 10, including its editor and program tests.
- **Renders:** `ViolinSynthTests "[.presettour]"` renders one phrase through every preset, and `"[.screenshot]"` writes the editor screenshot.

**Still to do for "done":** the plan's UX review and a save/restore check in several DAWs. These need you in FL Studio and any other hosts you use.
