#!/bin/sh
# bzip2 1.0.8's own `make test` (its Makefile's test target): the three
# shipped samples compressed at -1, -2 and -3 must equal the shipped .bz2
# files byte for byte, and the shipped .bz2 files must decompress (the third
# in small mode) to the samples.
# usage: make_test.sh path/to/bzip2 scratch-directory
set -eu
BZIP2=$1
WORK=$2
REF=$(cd "$(dirname "$0")/reference" && pwd)
rm -rf "$WORK"
mkdir -p "$WORK"
cd "$WORK"
cp "$REF"/sample1.ref "$REF"/sample2.ref "$REF"/sample3.ref .
cp "$REF"/sample1.bz2 "$REF"/sample2.bz2 "$REF"/sample3.bz2 .
"$BZIP2" -1  < sample1.ref > sample1.rb2
"$BZIP2" -2  < sample2.ref > sample2.rb2
"$BZIP2" -3  < sample3.ref > sample3.rb2
"$BZIP2" -d  < sample1.bz2 > sample1.tst
"$BZIP2" -d  < sample2.bz2 > sample2.tst
"$BZIP2" -ds < sample3.bz2 > sample3.tst
cmp sample1.bz2 sample1.rb2
cmp sample2.bz2 sample2.rb2
cmp sample3.bz2 sample3.rb2
cmp sample1.tst sample1.ref
cmp sample2.tst sample2.ref
cmp sample3.tst sample3.ref
echo "make test: all six checks passed"
