#!/usr/bin/env python3
"""Download the University of Iowa MIS violin (2012) arco recordings into research/external/iowa.

    python scripts/fetch_iowa.py

Anechoic chromatic runs on each string at pp, mf and ff (mono versions, about
36 files). Source: Lawrence Fritts, University of Iowa Electronic Music
Studios, https://theremin.music.uiowa.edu/MIS.html. The site states the
recordings may be downloaded and used for any projects, without restrictions.
"""

from __future__ import annotations

import re
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TARGET = ROOT / "external" / "iowa"
BASE = "https://theremin.music.uiowa.edu/"
INDEX = BASE + "MISviolin2012.html"


def main():
    TARGET.mkdir(parents=True, exist_ok=True)
    page = urllib.request.urlopen(INDEX).read().decode("utf-8", "replace")
    links = sorted(set(re.findall(r'href="([^"]*Violin\.arco\.[^"]*\.mono\.aif)"', page)))
    if not links:
        sys.exit("no arco mono files found on " + INDEX)
    failed = []
    for link in links:
        # Some links on the page contain stray whitespace (e.g. a tab); try the cleaned name too.
        candidates = [link, re.sub(r"\s+(?=\.mono)", "", link)]
        name = re.sub(r"\s+", "", link.rsplit("/", 1)[-1])
        dest = TARGET / name
        if dest.exists():
            continue
        print("downloading", name)
        for candidate in dict.fromkeys(candidates):
            try:
                urllib.request.urlretrieve(BASE + urllib.parse.quote(candidate), dest)
                break
            except urllib.error.HTTPError:
                continue
        else:
            failed.append(name)
    print(f"{len(links) - len(failed)} of {len(links)} files in {TARGET}")
    if failed:
        print("could not download:", ", ".join(failed))


if __name__ == "__main__":
    main()
