# bz2-ng specification

bz2-ng is a drop-in replacement for libbz2 1.0.8 with a much faster single-threaded encoder and decoder. This document says what the library must do, how it is structured, how it is verified, and in what order it gets built. The evidence behind each choice is in `research/` (report letters in brackets refer to files there); the prototypes are in `spikes/`.

## 1. Goals and non-goals

**Goals**

- **Drop-in.** Replacing libbz2 with bz2-ng is all a user does: same header, same symbols, same behaviour call for call, same bytes out. No recompilation of dependants.
- **Faster, never slower.** Both directions faster than libbz2 on every supported platform, at every level, for large files and for small independent streams alike.
- **General-purpose.** Large files, small streams (such as CRAM blocks of a few kilobytes), every level 1–9, all the legacy entry points.
- **Easy to adopt.** Plain C with no dependencies, buildable anywhere libbz2 builds, usable from C, C++, Rust and anything else that binds libbz2 today.

**Non-goals**

- **Parallelism.** The encoder and decoder are single-threaded. No block-parallel modes, now or as an option.
- **A new format.** Streams are bzip2 streams; anything bz2-ng writes libbz2 reads, and the reverse.
- **Better compression by default.** The default output is byte-identical to libbz2's (§2.3). Ratio-improving modes are opt-in and belong to the extended API (§4.8).

## 2. Compatibility contract

### 2.1 API and ABI

- The public header is libbz2 1.0.8's `bzlib.h`, unmodified.
- The library exports exactly the 24 public functions of `libbz2.def` (six low-level, eight `FILE*`-layer, two buffer-to-buffer, `BZ2_bzlibVersion`, and the seven zlib-style `BZ2_bz*open/read/write/flush/close/error` wrappers), enforced by a version script or export list. [E §2a]
- `bz_stream` layout, the `BZ_*` constants and return codes, `BZ_MAX_UNUSED`, and `BZ_API` (`WINAPI` on Windows) are unchanged. `EState`, `DState` and `bzFile` are private and free to change.
- `BZ_NO_STDIO` builds are supported: no `FILE*` layer, no diagnostics, and the integrator supplies `bz_internal_error`.
- `BZ2_bzlibVersion()` returns a string that begins `1.0.8, ` so that callers which parse a leading version keep working; the bz2-ng version follows the comma. [E §2a]
- Shared-library names follow the host: SONAME `libbz2.so.1.0` (upstream, Debian) or `libbz2.so.1` (Fedora, openSUSE), selectable at build time; `libbz2.1.0.dylib` install name on macOS; `bzip2.pc` for pkg-config.

### 2.2 Behaviour

For any sequence of calls a correct program can make, every call returns the same code as libbz2 1.0.8 and leaves `next_in`, `avail_in`, `next_out`, `avail_out` and the 64-bit totals with the same values. This includes:

- **Compression:** `BZ_RUN`, `BZ_FLUSH` and `BZ_FINISH` with input and output in pieces of any size down to zero and one byte; `BZ_PARAM_ERROR` for a `BZ_RUN` that makes no progress; `BZ_SEQUENCE_ERROR` when `avail_in` or the action changes during a flush or finish; `verbosity` left unvalidated in `BZ2_bzCompressInit`; `workFactor` validated to 0–250 (it selects nothing); a `bz_stream` that was copied after initialisation is rejected. [K §6]
- **Decompression:** resumable at any input and any output byte; input consumed exactly as libbz2 consumes it; the same streams accepted and rejected, with the same error code after the same output has been delivered. That covers incomplete and oversubscribed Huffman code sets, selector-count handling as in 1.0.8, randomised blocks, block and stream CRC errors, truncated input, and `small` mode. [J §1, §4]
- **Wrappers:** `BZ2_bzBuffToBuff*`, `BZ2_bzRead*`, `BZ2_bzWrite*` and the zlib-style functions are libbz2's own code over the new low-level functions, so concatenated streams, `BZ2_bzReadGetUnused` and `BZ_OUTBUFF_FULL` behave as before.
- **Allocation:** all memory through `bzalloc`/`bzfree`/`opaque`, allocated in the `Init` call so `BZ_MEM_ERROR` appears where callers expect it; no mutable static data. An allocator that returns memory aligned only to 4 bytes, which is enough for libbz2, must remain enough: the state is aligned internally.
- **Diagnostics:** the decoder's output on stderr matches libbz2's at every `verbosity`; the encoder's per-block and CRC lines match (§2.5 lists what is not reproduced).

### 2.3 Output

Compressed output is byte-identical to libbz2 1.0.8's for the same input, level and call sequence. Block boundaries therefore follow libbz2's rule exactly, including under `BZ_FLUSH`. [C2 §2.1, K]

There is one exception. A block that is exactly periodic after run-length encoding (the block is some string repeated a whole number of times; input of all zeros produces these) has several valid `origPtr` values. libbz2's choice depends on which of its internal sorters ran; bz2-ng always writes the last of the tied rows. Such streams differ in those 24 bits only and decode identically. No stream from any benchmark corpus is affected. [K §6, D]

Every kernel tier (§4.5) produces the same bytes; output never depends on the CPU.

### 2.4 Memory

| | Documented by libbz2 | bz2-ng |
|---|---|---|
| Compression | 400 kB + 8 × block size | inside the formula at every level; projected at about 8 × block size + 85 kB with the project's sorter (§7) |
| Decompression | 100 kB + 4 × block size | inside the formula, or one block more (100 kB + 5 × block size) if that is what the speed costs; see below |
| Decompression, `small` | 100 kB + 2.5 × block size | inside the formula at every level |

Memory is measured through a counting `bzalloc` in the test suite, at every level, and a build that exceeds its stated bound fails. [J §2, K §3]

**Decoder layout rule.** The in-formula layout costs 1–7% on full-size blocks against a layout with 4-byte table entries throughout, which needs one more block (about 900 kB at level 9). One extra block is an acceptable price for that speed, but only if nothing else recovers it: with every other decoder improvement in place, the in-formula layout, a cache-line-aligned 3-byte table and the 4-byte layout are measured on the reference machines, and the in-formula layout is kept if it reaches the same speed. If the larger layout becomes the default, it is listed in §2.5 and in the porting note.

### 2.5 Known divergences

These are the only places behaviour differs from libbz2 1.0.8. Each is deliberate.

| Behaviour | libbz2 | bz2-ng | Reason |
|---|---|---|---|
| `origPtr` of exactly periodic blocks | depends on which sorter ran | last tied row | §2.3 |
| A decompression call after an error return | resumes its state machine | returns the same error again | The manual permits only `BZ2_bzDecompressEnd` after an error; resuming reaches undefined behaviour inside libbz2 itself. [J §5] Open decision D2. |
| `bzalloc` request sizes | four requests, largest 3.6 MB at level 9 | fewer bytes in total, but the largest single request is larger | A caller whose pool is carved to libbz2's exact request sizes could fail. The total stays inside the documented formula, which is the documented contract. [K §6] |
| Encoder diagnostics at `verbosity` ≥ 2 that describe libbz2's sorter, and all detail at ≥ 3 | printed | not printed | They describe internals that no longer exist. |
| Exported private symbols (`BZ2_blockSort`, `BZ2_decompress`, `BZ2_crc32Table`, …) | leak from the shared library | not exported | Not part of the API. A reverse-dependency check precedes the first release (§7). Open decision D3. |

## 3. Performance requirements

Output and memory are fixed by §2, so "better" is speed. The reference is libbz2 1.0.8 built with the same compiler at its own flags, measured single-threaded in the same process on the same streams.

**Requirements**

1. **Never slower.** For every file in the benchmark corpus (`data/README.md`), every level 1–9, both directions, every supported platform and compiler: bz2-ng at its shipping configuration is at least as fast as libbz2.
2. **Small streams.** The same holds for independent streams from 1 kB to 64 kB, each with its own `Init` and `End`.
3. **Call pattern.** With caller buffers of 64 bytes or more, the legacy API runs within 10% of whole-buffer speed in both directions.
4. **No regression.** Every optimisation lands with a benchmark it must not regress; the benchmark suite runs on the reference machines for each release candidate.

**Where the prototypes stand**

Best measured, level 9, MB/s with the multiple of libbz2 on the same machine. One-shot prototypes with libsais as the sorter, clang builds. [P]

| Data | M4 Max enc / dec | Zen 5 enc / dec | Granite Rapids enc / dec | Graviton4 enc / dec |
|---|---|---|---|---|
| MC:Z, 640 kB streams | 66.5 (7.0×) / 446 (4.3×) | 79.1 (9.4×) / 419 (4.4×) | 62.1 (9.6×) / 250 (3.8×) | 52.2 (8.2×) / 212 (3.0×) |
| XA:Z, 1.65 MB streams | 72.2 (3.3×) / 284 (4.3×) | 68.9 (3.5×) / 250 (4.3×) | 55.4 (3.4×) / 167 (3.9×) | 47.9 (2.9×) / 136 (3.1×) |
| Silesia, 12 files | 63.1 (3.0×) / 230 (3.7×) | 60.4 (3.1×) / 192 (3.5×) | 49.7 (3.1×) / 139 (3.6×) | 43.6 (2.8×) / 120 (3.1×) |

Shipping configuration: the project's own sorter, behind the legacy API, inside the documented memory. Encode figures for the M4 and Graviton4 are measured [T]; the x86 encode figures are the table above multiplied by the measured end-to-end ratio of the two sorters [T], so they are projections. The legacy API costs the encoder nothing at calls of 5,000 bytes or more [K]. Decode on the M4 is measured through the legacy API [J]; server decode is expected at 0.93–1.00 of the table above (the cost of the in-formula layout on full blocks) and has not been measured with native x86 kernels behind the API.

| Data | M4 Max enc | Zen 5 enc (projected) | Granite Rapids enc (projected) | Graviton4 enc | M4 Max dec |
|---|---|---|---|---|---|
| MC:Z, 640 kB streams | 74.9 (7.9×) | 70 (8.3×) | 58 (8.9×) | 49.0 (7.7×) | 4.0× |
| XA:Z, 1.65 MB streams | 58.7 (2.7×) | 54 (2.7×) | 44 (2.7×) | 38.3 (2.3×) | 4.0× |
| Silesia, 12 files | 53.5 (2.5×) | 50 (2.6×) | 40 (2.6×) | 36.2 (2.4×) | 3.5× |

Two caveats apply to every number above. They are clang builds; gcc 13 builds of the same code are 2–31% slower in a few identifiable loops [P §7.6], and requirement 1 applies to gcc builds too. And the sorter is still being improved (§7, M3).

## 4. Architecture

### 4.1 Shape

```
bzlib.h            libbz2's public header, unmodified
legacy drivers     libbz2's compress and decompress state machines, kept line for line
stdio layer        libbz2's bzRead/bzWrite/bzopen wrappers and BuffToBuff, unmodified in logic
block encoder      input stage → least rotation → sorter → MTF and zero runs → Huffman → bit output
block decoder      header parser → symbol loop → table build → inverse-BWT walk → un-RLE1 and CRC
kernels            scalar, x86 SSE4, x86 AVX2, AArch64 NEON; selected at run time
```

**Repository layout.** The C library is self-contained in `c/` (sources, public header, tests, the bzip2 programs, its build files) and builds without reference to anything above that directory. Language bindings live beside it in the same repository: a Rust crate in `rust/` and a JNI library in `java/` (§7, M5), each compiling the C sources from `c/`.

The legacy drivers own all `bz_stream` bookkeeping. The block encoder and decoder work on caller-owned scratch memory and know nothing about `bz_stream`; they are what an extended API would call directly.

### 4.2 Encoder

- **Driver.** libbz2's state machine (states, modes, `avail_in_expect`, progress rules) unchanged. [K §2.3]
- **Input stage.** Run-length encoding and CRC over each caller chunk in bulk, with the run state carried between calls. The last run of every call stays pending until a later byte arrives, and a unit-by-unit tail makes each block-full decision, which is what reproduces libbz2's block boundaries. Calls of a few bytes take libbz2's per-byte path. [K §2.2]
- **Least rotation.** The block is rotated to its lexicographically least rotation so that a plain suffix sorter yields bzip2's cyclic order; exactly periodic blocks are sorted through their root. [D, S]
- **Sorter.** §4.3.
- **MTF and zero runs, Huffman, bit output.** The reworked stages of the standalone encoder with identical output: change-position scan for MTF, libbz2's table selection reproduced exactly, 64-bit bit writer. [S, H]
- **Output staging.** A finished block's bits are held in the tail of the sort workspace and released to the caller exactly as libbz2 releases them (whole bytes up to the end-of-block code; the remainder carried into the next block). [K §2.3]

### 4.3 Sorter boundary

One function is the whole interface between the encoder and the sorter:

```c
int32_t bzs_bwt(const uint8_t *T, uint8_t *L, int32_t n, int32_t q,
                int32_t *freq, int32_t *work, size_t work_ints);
```

`T` is the block's least rotation; the function writes the cyclic BWT to `L` (which may alias `T`) and the byte histogram to `freq`, and returns the row of rotation `q`, which is `origPtr`. It allocates nothing and has no global state. The workspace is `n + n/2 + 2048` ints, which is guaranteed sufficient for any input. [T §2]

The sorter is the project's own, derived from Yuta Mori's MIT-licensed sais-lite 2.4.1. libsais (Apache-2.0) is not part of the library: Apache-2.0 is incompatible with GPLv2-only programs that link a system libbz2. The sorter is written without reference to libsais's source, and every technique in it has a recorded origin (§8).

### 4.4 Decoder

- **Header parser.** libbz2's per-bit resumable structure, reading from a 64-bit buffer. Errors are raised after exactly the bits libbz2 would have read. [J §1]
- **Symbol loop.** A fast path decodes whole 50-symbol groups when at least 160 input bytes are available (table-driven Huffman, the head of the MTF list in a vector register); an exact path handles one symbol at a time near the end of the caller's input. Both give libbz2's results, including for code sets that are not complete prefix codes.
- **Block finishing.** Segmented table build, then a multi-chain inverse-BWT walk into a pool of chunks. Blocks above three quarters of the level's block size use 3-byte table entries so that the symbol array can become the walk's output pool; smaller blocks use 4-byte entries in the same allocation. The walk does no I/O.
- **Output.** A resumable un-RLE1 with CRC over each call's output, writing straight into the caller's buffer.
- **`small` mode.** libbz2's packed 2.5-byte table and single chain, with the fast symbol loop and a table lookup in place of the binary search. [J §2.4]
- **Tunables fixed by measurement:** 24 chains for full-size blocks (fewer for small blocks), software prefetch in the walk, four table-build segments on x86 and Apple silicon and two on other AArch64 cores. None of them affects output. [P §7]
- **Safety invariants** from the decoder's CVE history are enforced on every path; a fast path never reads or writes outside its buffers on hostile input. [F]

### 4.5 Kernels and run-time dispatch

- Every ISA-specific routine sits behind one table of function pointers per library, filled on first use from the CPU's features (`cpuid`/`xgetbv` on x86, `getauxval` on Linux AArch64, `sysctl` on macOS). **The tier is chosen at run time, never at compile time**: one binary carries all tiers for its architecture.
- Each tier is its own translation unit compiled with that tier's flags; the rest of the library uses the distribution's baseline flags.
- Granularity is one call per block per stage; the decoder's symbol loop is instantiated whole per tier so the MTF head can live in a register across symbols.

| Tier | Requires | Role |
|---|---|---|
| Scalar | portable C | Fallback for any CPU, and the reference every tier is tested against |
| x86 SSE4 | x86-64-v2 and PCLMULQDQ | Floor for x86-64; within 1–3% of AVX2 |
| x86 AVX2 | x86-64-v3; VPCLMULQDQ when present | Default on current x86 |
| AArch64 NEON | NEON and CRC32 | Default on AArch64 |

There is no AVX-512, SVE2 or SIMDe tier: each measured at 0–3% or less over the tiers above. [P §6, §9] An environment variable forces a tier, for tests and measurement only.

CRC kernels: slice-by-16 (scalar), ARMv8 `crc32` instructions, carry-less multiplication folding on x86.

### 4.6 Determinism

All stages are exact integer computations. Tests require every tier available on the test machine to produce identical bytes, in both directions.

### 4.7 Portability

C11, no compiler extensions without a fallback. Little- and big-endian, 32- and 64-bit. gcc, clang and MSVC. Platforms without a vector tier run the scalar tier and must still meet §3.

### 4.8 Extended API

Deferred until after the drop-in is complete (§7, M5). It adds no speed; its value is reusable contexts with input-sized memory, and options the legacy API has no slot for. The one ratio option planned is a guard that codes each block both libbz2's way and an alternative way and emits the smaller, which is never larger than libbz2 by construction and gains 0.13–0.37% for 5–9% more encode time. [H3] Everything in the extended API lives under a new symbol prefix and a separate header.

## 5. Verification

- **Differential conformance.** A tester links stock libbz2 1.0.8 under renamed symbols beside bz2-ng and drives both with identical call sequences, comparing return code, stream pointers, totals and bytes after every call. Compression: every split point for small inputs, random `RUN`/`FLUSH`/`FINISH` sequences, output space down to zero, inputs around block capacity, every level. Decompression: every input and output split for small streams, both `small` settings, mutated and truncated streams, concatenated streams through `BZ2_bzRead`, allocation failures. [J §4, K §5]
- **Cross-decoding.** Everything bz2-ng writes is decoded by stock libbz2, and the reverse.
- **Sorter.** Output compared with an independent oracle (cyclic prefix doubling, itself checked against a naive rotation sort) on generated families: exhaustive small alphabets, every short length, Fibonacci and Thue–Morse words, de Bruijn sequences, long runs, periodic and near-periodic blocks, worst-case inputs for the workspace bound, random lengths to the maximum block. [T §4]
- **Every tier.** All of the above run for each kernel tier the machine supports, and tiers are compared with each other.
- **Sanitisers and fuzzing.** The suites run under AddressSanitizer and UndefinedBehaviorSanitizer. Fuzz targets for the decoder, the encoder and the sorter run differentially against libbz2 or the oracle.
- **Memory.** Counting-allocator tests against the formulas of §2.4 at every level.
- **Drop-in tests.** bzip2's own `make test`; the `bzip2` program built against bz2-ng compared with stock bzip2 across option combinations (stdout, stderr, exit status); CPython's `test_bz2` with the library preloaded; further downstream suites before release (§7, M4).
- **ABI.** The exported symbol list is checked against the 24; `bz_stream` layout is checked by a compile-time test.
- **Tests generate their data.** The corpora are for benchmarks and bulk checks; unit and conformance tests build their inputs in code.

## 6. Build and continuous integration

- CMake is the build system, producing a static and a shared library, the `bzip2` and `bzip2recover` programs from libbz2's unmodified sources, and `bzip2.pc`.
- The build stays simple enough for another build system to reproduce from a short description, because the bindings compile the C sources directly: a flat list of sources, per-tier compiler flags confined to the tier translation units, no generated code, and CPU features detected at run time rather than at configure time.
- CI builds and tests with **both gcc and clang** on Linux x86-64 and AArch64 and on macOS, with sanitiser jobs, a `BZ_NO_STDIO` job, and emulated big-endian and 32-bit jobs. Windows follows in M2.
- A performance job on fixed reference machines compares against libbz2 for release candidates, for gcc and clang builds: the distribution's default version of each and the latest release of each.

## 7. Milestones

| | Milestone | Done when |
|---|---|---|
| M1 | **Integration.** One library containing the encoder driver, the decoder driver, the project's sorter, and the scalar, SSE4, AVX2 and NEON tiers with run-time dispatch, over libbz2's stdio layer. | Both differential testers pass on every tier; bzip2's `make test` and CPython's `test_bz2` pass; memory is inside every formula, measured; speed within 3% of the prototypes at the shipping configuration on the four reference machines. |
| M2 | **Hardening.** Fuzz targets, the CI matrix of §6, export list and ABI checks, `BZ_NO_STDIO`, Windows, big-endian and 32-bit. | CI green on the full matrix; a fuzzing campaign of agreed length finds nothing. |
| M3 | **Performance.** Sorter improvements; the gcc-specific slow loops; the decoder layout decision of §2.4 (including a cache-line-aligned 3-byte table); a wider MTF head across several registers; an interleaved or PMULL-folding CRC on AArch64 in place of the single-chain `crc32` kernel (libdeflate's approach, MIT; CRC is 2–4% of decode on Apple silicon today). | Requirement 1 of §3 met with gcc and clang on every reference machine; each change carries its benchmark. |
| M4 | **Drop-in validation.** Rebuild-free substitution on a real distribution: the test suites of a chosen set of dependants run against bz2-ng; packaging names; a porting note listing §2.5. | The chosen suites pass; private-symbol reverse-dependency check done. |
| M5 | **Extended API and bindings.** The extended API; a Rust `-sys` crate with a safe wrapper in `rust/`; a JNI library in `java/`. | Reusable contexts and the guarded ratio mode available under the new prefix; both bindings build from the sources in `c/` and pass their own round-trip and cross-decoding tests; documented. |

**Integration risk in M1.** The project's sorter has not yet run behind the legacy encoder driver. The driver stages each compressed block in the tail of the sort workspace, and that arrangement was measured with a different sorter's workspace layout. Arithmetic says the total fits (about 8 × block size + 85 kB against 400 kB + 8 × block size), but it is unmeasured until M1.

**Before any public release:** the sorter's provenance record is complete and reviewed (§8, open decision D5); licence statements for the benchmark data are verified; libsais, SIMDe and other spike-only code are confirmed absent from the library.

## 8. Licensing and provenance

- The library's own code is MIT. Files derived from bzip2 1.0.8 (the drivers and the stdio layer) keep bzip2's BSD-style licence and are marked as altered, as that licence requires. The sorter keeps sais-lite's MIT notice.
- Code under GPL, LGPL or Apache-2.0 is never copied or translated. For libsais specifically, the rule is stricter than process: the result must not be something that could reasonably be called a copy. Its source, disassembly and per-symbol profiles are never examined; designs that follow from the published papers, sais-lite and libdivsufsort are preferred; and the sorter's report carries a table giving the origin of every technique.
- `THIRD_PARTY.md` lists third-party code in the repository. Nothing listed there as spike-only ships.

## 9. Open decisions

| | Decision | Recommendation |
|---|---|---|
| D2 | **Calls after a decompression error.** Return the same error again, or emulate libbz2's resumption. | Return the same error. |
| D3 | **Private symbols.** Export only the 24 public functions, or keep exporting libbz2's leaked internals as stubs. | Only the 24, after the reverse-dependency check in M4. |
| D5 | **Sorter release gate.** What review of the sorter and its provenance record is required before a public release. | Provenance table complete, plus an independent read of the final sorter against the clean-room rule. |
| D6 | **Programs.** Ship `bzip2` and `bzip2recover` built from libbz2's unmodified sources. | Yes: a distribution replaces the package, not only the library, and the tests need them. |
