"""Build the note audit page: python3 build.py notes.json out.html (notes.json from notecompare.py).
Publish with clips/finale-real.mp3 and clips/finale-d3.mp3 (level-matched, full length, same start)."""
import os, sys
t = open(os.path.join(os.path.dirname(__file__), 'template.html')).read()
open(sys.argv[2], 'w').write(t.replace('/*DATA*/null', open(sys.argv[1]).read()))
