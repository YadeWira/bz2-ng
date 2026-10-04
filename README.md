# bz2-ng
API compatible next-generation port of libbz2 / libbzip2

## Status

Specification and prototypes; there is no library yet. The goal is a drop-in C replacement for libbz2 with a faster single-threaded encoder and decoder, the same or better compression ratio, and streams that libbz2 reads and writes interchangeably.

## Layout

- `c/` — the C library (in progress): sources, public header, tests and build files.
- `docs/spec.md` — the specification: compatibility contract, performance requirements, architecture, verification and milestones.
- `research/` — research reports and spike write-ups; start with `research/README.md`.
- `spikes/` — measurement prototypes, each with its own `README.md` and `run.sh`. Not library code.
- `data/` — benchmark corpora: scripts to build or fetch them, and the 1000 Genomes-derived tag and quality corpora in Git LFS; see `data/README.md`.
- `THIRD_PARTY.md` — third-party code in the tree and its licences.
