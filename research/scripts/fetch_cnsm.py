#!/usr/bin/env python3
"""Download the CNSM violin dataset (CC BY 4.0) into research/external/cnsm.

    python scripts/fetch_cnsm.py               # admittances only (~40 MB extracted)
    python scripts/fetch_cnsm.py --recordings  # also the scale recordings

The full archive is 654 MB and is downloaded once to research/external/.
Credit: Pauget Ballesteros, H. (2026). CNSM Dataset (1.0.0). Zenodo.
https://doi.org/10.5281/zenodo.18696786
"""

from __future__ import annotations

import argparse
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXTERNAL = ROOT / "external"
URL = "https://zenodo.org/api/records/18696786/files/dataset.zip/content"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--recordings", action="store_true", help="also extract the audio recordings")
    parser.add_argument("--zip", type=Path, help="use an already downloaded dataset.zip")
    args = parser.parse_args()

    EXTERNAL.mkdir(exist_ok=True)
    archive = args.zip or EXTERNAL / "cnsm_dataset.zip"
    if not archive.exists():
        print(f"downloading {URL} (654 MB) ...")
        with urllib.request.urlopen(URL) as response, open(archive, "wb") as out:
            shutil.copyfileobj(response, out)

    target = EXTERNAL / "cnsm"
    prefixes = ["admittances/"] + (["recordings/"] if args.recordings else [])
    with zipfile.ZipFile(archive) as z:
        members = [m for m in z.namelist() if any(m.startswith(p) for p in prefixes)]
        z.extractall(target, members)
    print(f"extracted {len(members)} entries to {target}")


if __name__ == "__main__":
    sys.exit(main())
