# Guidance for agents working in this repository

bz2-ng is a drop-in replacement for libbz2 1.0.8 with a faster single-threaded encoder and decoder. `docs/spec.md` is the specification; read it before changing or reviewing anything under `c/`.

## Hard rules

- **libsais is off limits.** Never open libsais's source, README, benchmarks, changelog, issues, disassembly or per-symbol profiles, anywhere: not in `spikes/*/vendor/libsais`, not in any local clone, not on the web. This applies to reviewers and researchers as well as implementers. Do not suggest using libsais or doing something "the way libsais does". The library's sorter must not be something that could be called a copy of it; libsais is Apache-2.0, which is incompatible with GPLv2-only programs that link a system libbz2. It may be linked as a black box in a spike for comparison, and timed with whole-process counters only.
- **No GPL or LGPL code** is read for code or copied (lbzip2, 7-Zip's bzip2 encoder, sdsl-lite and similar). MIT, BSD, zlib-licensed and public-domain code may be reused with its notice kept and altered files marked.
- **Output is byte-identical to libbz2's** and the legacy API matches libbz2 call for call (`docs/spec.md` §2). A change that alters either is a bug unless the spec lists it as a divergence.
- **Single-threaded.** No parallel encoder or decoder modes.

## The C library (`c/`)

- **Style and structure follow libdeflate** (`c/.clang-format`; `docs/spec.md` §4.8): tabs at width 8, 80 columns, kernel-style braces, `common_defs.h`, CPU feature detection and kernels under `lib/x86/` and `lib/arm/`, self-replacing dispatch pointers, template headers.
- **Files that are libbz2's own code keep upstream formatting**: `c/bzlib.h`, `c/programs/`, `c/test/reference/` are unmodified; `c/lib/bzlib.c` and `bzlib_private.h` are lightly altered. The legacy drivers (`compress.c`, `decompress.c`) deliberately keep libbz2's state machines recognisable.
- **Kernel tiers are chosen at run time** and every tier must produce the same bytes. `BZ2NG_TIER` exists for tests.
- **Correctness evidence is the differential testers**, which drive stock libbz2 and bz2-ng with identical calls. Build and test: `cmake -S c -B c/build && cmake --build c/build && ctest --test-dir c/build`.
- Every optimisation needs a benchmark it must not regress. Timings are single-threaded and compared with libbz2 built by the same compiler. Time a change in rounds interleaved with its baseline and with a second build of the baseline as a control: code placement alone moves a cell by 1–3% (`research/M1d-decoder-tuning.md`, `research/M1f-table-build-order.md`). Where a change gains on one compiler and machine and loses on another, clang on AArch64 and gcc on x86-64 count most: a small loss with gcc on AArch64 or clang on x86-64 is acceptable for a gain there (`docs/spec.md` §3).

## Writing

- Comments say why, describe the code as it is, and carry no history.
- Markdown prose is not hard-wrapped. British English.
- Tests generate their data in code; the corpora under `data/` are for benchmarks and bulk checks.

## Repository

- `research/` holds the reports behind each decision (`research/README.md` is the index); `spikes/` holds the prototypes they measured. Neither is library code.
- CI runs on Linux runners only, at milestones and on merges to `main`, not on every push.
