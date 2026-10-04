#!/usr/bin/env python3
"""Summarises bench TSV files (bench/bench.c rows: set, level, chunk, tier,
implementation, operation, round, MB/s): per set and level, the median MB/s
of every implementation and operation, the spread ((max - min) / median, the
worst of the row) and the ratios bz2-ng / libbz2 and wide / in-formula layout.
Malformed rows are an error.
usage: bench_summary.py file.tsv label [file.tsv label ...]
"""
import statistics
import sys
from collections import defaultdict

IMPLS = ('libbz2', 'bz2ng', 'bz2ng_fit', 'bz2ng_wide')


def load(path):
    rows = defaultdict(list)
    tiers = set()
    with open(path) as f:
        for number, line in enumerate(f, 1):
            fields = line.rstrip('\n').split('\t')
            if len(fields) != 8:
                sys.exit(f'{path}:{number}: expected 8 tab-separated fields, found {len(fields)}')
            name, level, _chunk, tier, impl, op, _round, mbs = fields
            if impl not in IMPLS or op not in ('c', 'd'):
                sys.exit(f'{path}:{number}: unknown implementation or operation: {impl} {op}')
            try:
                rows[(name, int(level), impl, op)].append(float(mbs))
            except ValueError:
                sys.exit(f'{path}:{number}: level or MB/s is not a number')
            tiers.add(tier)
    if not rows:
        sys.exit(f'{path}: no rows')
    return rows, tiers


def summarise(path, label):
    rows, tiers = load(path)
    keys = sorted({(k[0], k[1]) for k in rows})
    print(f'### {label} (tier {", ".join(sorted(tiers))})\n')
    print('| data | level | rounds | libbz2 c | bz2-ng c | c × | libbz2 d | bz2-ng d (wide) | d × | in-formula d | wide / in-formula | max spread |')
    print('|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|')
    for name, level in keys:
        def med(impl, op):
            v = rows.get((name, level, impl, op), [])
            if not v:
                return 0.0, 0.0, 0
            m = statistics.median(v)
            return m, (max(v) - min(v)) / m, len(v)
        rc, s1, n = med('libbz2', 'c')
        nc, s2, _ = med('bz2ng', 'c')
        rd, s3, _ = med('libbz2', 'd')
        wd, s4, _ = med('bz2ng_wide', 'd')
        fd, s5, _ = med('bz2ng_fit', 'd')
        if 0 in (rc, nc, rd, wd, fd):
            sys.exit(f'{path}: {name} level {level} lacks an implementation')
        spread = max(s1, s2, s3, s4, s5) * 100
        print(f'| {name} | {level} | {n} | {rc:.1f} | {nc:.1f} | {nc / rc:.2f} | {rd:.1f} | {wd:.1f} | {wd / rd:.2f} | {fd:.1f} | {wd / fd:.3f} | {spread:.0f}% |')
    print()


args = sys.argv[1:]
if not args or len(args) % 2:
    sys.exit(__doc__)
for i in range(0, len(args), 2):
    summarise(args[i], args[i + 1])
