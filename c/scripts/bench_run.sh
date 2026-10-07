#!/bin/sh
# Times bz2-ng against stock libbz2 on the standard data sets (bench/bench.c):
# the published NA12878 MC:Z tag values in 640,000-byte streams, its XA:Z
# values in 1,650,000-byte streams, the first 4 MB of each in 16,384-byte
# streams, and the twelve Silesia files whole (data/README.md).
# Appends TSV rows to OUT.  On Linux every timing runs pinned to CPU 1; take
# the machine's timing lock around this script.
# usage: scripts/bench_run.sh BENCH OUT LEVEL [ROUNDS] [DATA]
#   DATA defaults to the repository's data/ directory (../../data from here).
set -eu
if [ $# -lt 3 ]; then
	echo "usage: scripts/bench_run.sh BENCH OUT LEVEL [ROUNDS] [DATA]" >&2
	exit 2
fi
BENCH=$1
OUT=$2
LEVEL=$3
ROUNDS=${4:-5}
DATA=${5:-$(dirname "$0")/../../data}
if [ ! -x "$BENCH" ]; then
	echo "bench_run.sh: no benchmark program at $BENCH (configure with BZ2NG_BUILD_BENCH=ON)" >&2
	exit 2
fi
for f in corpora/1kg/NA12878/MC.bin corpora/1kg/NA12878/XA.bin silesia/dickens; do
	if [ ! -r "$DATA/$f" ]; then
		echo "bench_run.sh: $DATA/$f not found; see data/README.md for the corpora, or pass DATA" >&2
		exit 2
	fi
done
PIN=""
if [ "$(uname -s)" = Linux ]; then
	PIN="taskset -c 1"
fi
SIL=""
for f in dickens mozilla mr nci ooffice osdb reymont samba sao webster x-ray xml; do
	SIL="$SIL${SIL:+,}$DATA/silesia/$f"
done
$PIN "$BENCH" -r "$ROUNDS" -l "$LEVEL" -c 640000 -o "$OUT" "mc640k=$DATA/corpora/1kg/NA12878/MC.bin"
$PIN "$BENCH" -r "$ROUNDS" -l "$LEVEL" -c 1650000 -o "$OUT" "xa1650k=$DATA/corpora/1kg/NA12878/XA.bin"
$PIN "$BENCH" -r "$ROUNDS" -l "$LEVEL" -c 16384 -n 4000000 -o "$OUT" "mc16k=$DATA/corpora/1kg/NA12878/MC.bin" "xa16k=$DATA/corpora/1kg/NA12878/XA.bin"
$PIN "$BENCH" -r "$ROUNDS" -l "$LEVEL" -c 0 -o "$OUT" "silesia=$SIL"
