# bz2-ng
API compatible next-generation port of libbz2 / libbzip2

## Status

A first library is in `c/`: the complete libbz2 API over the new encoder and decoder, byte-identical to libbz2's output, with kernels chosen at run time. It passes its conformance tests on x86-64 and AArch64, and under emulation on big-endian and 32-bit targets, and compresses about 2.3 to 9 times faster and decompresses about 1.6 to 4.7 times faster than libbz2 at level 9, depending on the data (`research/M3a-baseline.md`). It is not yet hardened for release: there is no Windows build yet and the long fuzzing campaign has not run (see `docs/spec.md`, milestones). The goal is a drop-in C replacement for libbz2 with a faster single-threaded encoder and decoder, the same or better compression ratio, and streams that libbz2 reads and writes interchangeably.

## Layout

- `c/` — the C library: sources in `c/lib/`, the public header `c/bzlib.h`, tests, and the `bzip2` programs. Build and test with `cmake -S c -B c/build && cmake --build c/build && ctest --test-dir c/build`.
- `docs/spec.md` — the specification: compatibility contract, performance requirements, architecture, verification and milestones.
- `research/` — research reports and spike write-ups; start with `research/README.md`.
- `spikes/` — measurement prototypes, each with its own `README.md` and `run.sh`. Not library code.
- `data/` — benchmark corpora: scripts to build or fetch them, and the 1000 Genomes-derived tag and quality corpora in Git LFS; see `data/README.md`.
- `THIRD_PARTY.md` — third-party code in the tree and its licences.
