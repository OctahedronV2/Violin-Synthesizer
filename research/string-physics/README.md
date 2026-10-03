# String-physics lab

Research for the world-class violin engine: bowed-string and bow physics, measured.

- `FINDINGS.md`: results and ranked engine changes. Start here.
- `literature.md`: survey of ~50 sources with cited numbers.
- `lab/`: four strings on one bridge, every physical ingredient switchable (`g++ -O2 -std=c++17 -o /tmp/lab lab/lab.cpp`; usage in the lab.cpp header).
- `tools/`: scoring scripts (Schelleng limits vs measured data, Guettler attacks, Iowa realism, clips). They expect the Iowa reference notes in `refs/iowa/`, which live in the project's shared folder (`research/world-class/string-physics/refs/`), not in git.
- `results/`: experiment tables and per-run JSON, fitted string data, bridge modes.
