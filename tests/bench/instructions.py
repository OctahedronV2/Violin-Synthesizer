#!/usr/bin/env python3
"""Instructions per output sample from a Callgrind run of ViolinSynthBench,
compared with the committed baseline (docs/PHASE7.md, step 7.0).

    valgrind --tool=callgrind --instr-atstart=no --callgrind-out-file=cg/callgrind.out.%p \
        build/tests/ViolinSynthBench --ci --seconds 1 --json cg/bench.json
    python3 tests/bench/instructions.py cg                # compare, fail on a regression
    python3 tests/bench/instructions.py cg --update       # write a new baseline

Instruction counts are stable on shared CI runners, where timings are not.
A scenario fails when it needs more than 3% more instructions per sample than
the baseline. A change meant to speed things up updates the baseline in the
same pull request, so the gain shows in the diff.
"""

import argparse
import json
import pathlib
import re
import sys

BASELINE = pathlib.Path(__file__).resolve().parents[2] / "docs" / "benchmarks" / "baseline.json"
TOLERANCE = 0.03


def read_run(directory: pathlib.Path) -> dict[str, float]:
    bench = json.loads((directory / "bench.json").read_text())
    samples = {s["name"]: s["outputSamples"] for s in bench["scenarios"]}

    counts: dict[str, int] = {}
    for dump in directory.glob("callgrind.out.*"):
        text = dump.read_text(errors="replace")
        name = re.search(r"^desc: Trigger: Client Request: (.+)$", text, re.M)
        total = re.search(r"^summary: (\d+)$", text, re.M)
        if name and total:
            counts[name.group(1)] = int(total.group(1))

    missing = sorted(set(samples) - set(counts))
    if missing:
        sys.exit(f"No Callgrind dump for: {', '.join(missing)}")
    return {name: counts[name] / samples[name] for name in samples}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("directory", type=pathlib.Path, help="folder with bench.json and the callgrind.out files")
    parser.add_argument("--update", action="store_true", help="write the results as the new baseline")
    args = parser.parse_args()

    run = read_run(args.directory)

    if args.update:
        BASELINE.parent.mkdir(parents=True, exist_ok=True)
        BASELINE.write_text(json.dumps({k: round(v, 1) for k, v in sorted(run.items())}, indent=2) + "\n")
        print(f"Wrote {BASELINE}")
        return 0

    baseline = json.loads(BASELINE.read_text())
    failed = []
    print(f"{'scenario':<18} {'baseline':>10} {'now':>10} {'change':>8}   (instructions per output sample)")
    for name in sorted(set(run) | set(baseline)):
        if name not in run or name not in baseline:
            print(f"{name:<18} {'new' if name in run else 'removed':>10}")
            continue
        change = run[name] / baseline[name] - 1.0
        flag = "  <-- slower" if change > TOLERANCE else ("  faster" if change < -TOLERANCE else "")
        print(f"{name:<18} {baseline[name]:>10.0f} {run[name]:>10.0f} {100 * change:>+7.1f}%{flag}")
        if change > TOLERANCE:
            failed.append(name)

    if failed:
        print(f"\n{len(failed)} scenario(s) need more than {100 * TOLERANCE:.0f}% more instructions: {', '.join(failed)}")
        print("If the extra work is intended, run this script with --update and commit the new baseline.")
        return 1
    print("\nNo scenario is slower than the baseline.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
