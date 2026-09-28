# Host checks (Phase 7)

Hand checks for the hosts that must work: **FL Studio, Reaper and Ableton Live on Windows 11** (see [PHASE7.md](PHASE7.md), step 7.6). Use the VST3 from the latest Windows CI build (`Octavio-VST3-Windows.zip`). Copy `Octavio.vst3` to `C:\Program Files\Common Files\VST3` and rescan plugins in each host.

For each check, write ✓, ✗ with what happened, or n/a. Put the date and the build (commit or PR) at the top of the column.

## Checklist

| # | Check | FL Studio | Reaper | Ableton Live |
|---|---|---|---|---|
| | Build / date | | | |
| 1 | The plugin loads as an instrument and plays from a MIDI keyboard or the piano roll | | | |
| 2 | The computer keyboard plays it with the plugin window open (FL: typing keyboard to piano; Reaper: virtual MIDI keyboard with *Send all keyboard input*; Ableton: computer MIDI keyboard, M key) | | | |
| 3 | It still plays after clicking a knob, the bow pad, a drop-down and an articulation button | | | |
| 4 | Velocity: 64 sounds natural and 100 is not scratchy. **Octave +1** makes the lowest typing key play C4 | | | |
| 5 | Right-click on a knob shows the host's menu (FL: *Create automation clip*). Try a knob, a drop-down, a switch, an articulation button and the bow pad | | | |
| 6 | Record automation of three controls, then play it back | | | |
| 7 | Save the project, close the host, reopen it: the sound, the preset name and any edits come back | | | |
| 8 | Change the sample rate (44.1 and 48 kHz) and the buffer size (64 and 512) with the project open: no crash, still in tune | | | |
| 9 | Render or export the project: the file sounds the same as playback, and a note lines up with a click track | | | |
| 10 | Open, close and resize the editor, including at 150% Windows display scaling | | | |
| 11 | 16 instances each playing a line: note the host's CPU meter | | | |

Notes on host behaviour go below, one heading per host.

## FL Studio

## Reaper

## Ableton Live
