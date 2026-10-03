#!/bin/sh
# Builds the Octavio 2 offline renderer: ./octavio2/render/build.sh [out] (default /tmp/o2)
set -e
cd "$(dirname "$0")/../.."
out=${1:-/tmp/o2}
obj=${TMPDIR:-/tmp}/o2-pffft.o
[ -f "$obj" ] || cc -O2 -DNDEBUG -c third_party/pffft/pffft.c -o "$obj"
g++ -O2 -std=c++17 -Isource -Ithird_party/pffft -o "$out" octavio2/render/render.cpp source/dsp/PartitionedConvolution.cpp "$obj" -lm
