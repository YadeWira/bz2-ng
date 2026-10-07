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
- **Better compression by default.** The default output is byte-identical to libbz2's (§2.3). Ratio-improving modes are opt-in and belong to the extended API (§4.9).

## 2. Compatibility contract

### 2.1 API and ABI

- The public header is libbz2 1.0.8's `bzlib.h`, unmodified.
- The library exports exactly the 24 public functions of `libbz2.def` (six low-level, eight `FILE*`-layer, two buffer-to-buffer, `BZ2_bzlibVersion`, and the seven zlib-style `BZ2_bz*open/read/write/flush/close/error` wrappers), enforced by a version script, an export list, or on Windows a module-definition file that also keeps the names undecorated on 32-bit x86, as libbz2's own `libbz2.def` does. [E §2a, M2c]
- `bz_stream` layout, the `BZ_*` constants and return codes, `BZ_MAX_UNUSED`, and `BZ_API` (`WINAPI` on Windows) are unchanged. `EState`, `DState` and `bzFile` are private and free to change.
- `BZ_NO_STDIO` builds are supported: no `FILE*` layer, no diagnostics, and the integrator supplies `bz_internal_error`.
- `BZ2_bzlibVersion()` returns a string that begins `1.0.8, ` so that callers which parse a leading version keep working; the bz2-ng version follows the comma. [E §2a]
- Shared-library names follow the host: SONAME `libbz2.so.1.0` (upstream, Debian) or `libbz2.so.1` (Fedora, openSUSE), selectable at build time; `libbz2.1.0.dylib` install name on macOS; on Windows `libbz2.dll` with mingw-w64 and `bz2.dll` with MSVC, whose static library is then `bz2_static.lib` beside the DLL's import library `bz2.lib`, as in libbz2's own CMake build (the names a Windows package uses are decided in M4) [M2f]; `bzip2.pc` for pkg-config.

### 2.2 Behaviour

For any sequence of calls a correct program can make, every call returns the same code as libbz2 1.0.8 and leaves `next_in`, `avail_in`, `next_out`, `avail_out` and the 64-bit totals with the same values. This includes:

- **Compression:** `BZ_RUN`, `BZ_FLUSH` and `BZ_FINISH` with input and output in pieces of any size down to zero and one byte; `BZ_PARAM_ERROR` for a `BZ_RUN` that makes no progress; `BZ_SEQUENCE_ERROR` when `avail_in` or the action changes during a flush or finish; `verbosity` left unvalidated in `BZ2_bzCompressInit`; `workFactor` validated to 0–250 (it selects nothing); a `bz_stream` that was copied after initialisation is rejected. [K §6]
- **Decompression:** resumable at any input and any output byte; input consumed exactly as libbz2 consumes it; the same streams accepted and rejected, with the same error code after the same output has been delivered. That covers incomplete and oversubscribed Huffman code sets, selector-count handling as in 1.0.8, randomised blocks, block and stream CRC errors, truncated input, and `small` mode. [J §1, §4]
- **Buffers:** neither direction modifies the caller's output buffer beyond the bytes it delivers, even where `avail_out` would allow it; the differential testers compare the buffers beyond the delivered bytes, not only the delivered bytes. Neither direction reads outside `[next_in, next_in + avail_in)`. Within that range both may load a few bytes ahead of what they consume (the decoder's 64-bit refill, the encoder's run scan); those bytes are not consumed, are read again on the next call, and never affect the result. A caller that passes an `avail_in` larger than its buffer and relies on decompression stopping at the end of the stream is outside the contract: libbz2 happens never to touch the bytes after a stream's end, and bz2-ng may load a few of them, as zlib's fast inflate loop does.
- **Wrappers:** `BZ2_bzBuffToBuff*`, `BZ2_bzRead*`, `BZ2_bzWrite*` and the zlib-style functions are libbz2's own code over the new low-level functions, so concatenated streams, `BZ2_bzReadGetUnused` and `BZ_OUTBUFF_FULL` behave as before.
- **Allocation:** all memory through `bzalloc`/`bzfree`/`opaque`, allocated in the `Init` call so `BZ_MEM_ERROR` appears where callers expect it. The only mutable process-wide data are the cached CPU feature mask and the dispatch pointers, which are written once in a way that is safe under concurrent first use. An allocator that returns memory aligned only to 4 bytes, which is enough for libbz2, must remain enough: the state is aligned internally.
- **Diagnostics:** the decoder's output on stderr matches libbz2's at every `verbosity`; the encoder's per-block and CRC lines match (§2.5 lists what is not reproduced).

### 2.3 Output

Compressed output is byte-identical to libbz2 1.0.8's for the same input, level and call sequence. Block boundaries therefore follow libbz2's rule exactly, including under `BZ_FLUSH`. [C2 §2.1, K]

There is one exception. A block that is exactly periodic after run-length encoding (the block is some string repeated a whole number of times; input of all zeros produces these) has several valid `origPtr` values. libbz2's choice depends on which of its internal sorters ran; bz2-ng always writes the last of the tied rows. Such streams differ in those 24 bits only and decode identically. No stream from any benchmark corpus is affected. [K §6, D]

Every kernel tier (§4.5) produces the same bytes; output never depends on the CPU.

### 2.4 Memory

| | Documented by libbz2 | bz2-ng |
|---|---|---|
| Compression | 400 kB + 8 × block size | inside the formula at every level: 8 × block size + 84,623 bytes on 64-bit targets and 40–50 bytes less on 32-bit ones, measured [M1, M2a] |
| Decompression | 100 kB + 4 × block size | one block more by default: 100 kB + 5 × block size; an in-formula layout is a build option; see below |
| Decompression, `small` | 100 kB + 2.5 × block size | inside the formula at every level |

Memory is measured through a counting `bzalloc` in the test suite, at every level, and a build that exceeds its stated bound fails. [J §2, K §3] The bounds are the same on 32-bit targets; there the peaks are 40–80 bytes lower, because the state structures hold smaller pointers. [M2a]

**Decoder layout rule.** A layout with 4-byte table entries throughout needs one more block than libbz2 documents (about 900 kB at level 9). The in-formula layout stores 3-byte entries for blocks above three quarters of the level's block size and so must recover each row's output symbol from a 16 kB guess table; it walks 24 chains like the default, in memory freed by keeping that guess table in the Huffman tables' space, which nothing else uses between a block's symbol loop and the next block header. One extra block is an acceptable price for speed, but only if nothing else recovers it. Measured in the library on the reference machines [M3d], the in-formula layout runs within 2% of the 4-byte layout's speed on Granite Rapids and at its speed on blocks of at most three quarters of the block size everywhere, but on full-size blocks it is 1–3% slower on the M4 and 5–7% slower on Zen 5 and Graviton4; the remaining cost is the 3-byte walk step and, on Graviton4, the 3-byte table build. So the 4-byte layout is the default and is listed in §2.5. The in-formula layout stays available as a build option for callers that size memory pools by libbz2's formula. A cache-line-aligned 3-byte table was tried and does not fit the formula (42.9 kB of padding at level 9 against 12.3 kB spare); it gains 2–4% on Graviton4 only [M3d].

### 2.5 Known divergences

These are the only places behaviour differs from libbz2 1.0.8. Each is deliberate.

| Behaviour | libbz2 | bz2-ng | Reason |
|---|---|---|---|
| `origPtr` of exactly periodic blocks | depends on which sorter ran | last tied row | §2.3 |
| A decompression call after an error return | resumes its state machine | returns the same error again | The manual permits only `BZ2_bzDecompressEnd` after an error; resuming reaches undefined behaviour inside libbz2 itself. [J §5, M1e] One case is unchanged from libbz2: after a combined-CRC mismatch at the end of a stream the stream is finished, and the next call returns `BZ_SEQUENCE_ERROR`. Open decision D2. |
| Decompression memory | 100 kB + 4 × block size | 100 kB + 5 × block size by default | §2.4; 5–7% faster on full-size blocks on Zen 5 and Graviton4, 1–3% on the M4, 0–2% on Granite Rapids. A build option restores the documented formula. |
| `bzalloc` request sizes | compression: four requests, largest 3.6 MB at level 9; decompression: the state and one table | compression: fewer bytes in total, but the largest single request is larger; decompression: the state and one larger request, and even the in-formula layout uses 14–24 kB more in total than libbz2 itself does | A caller whose pool is carved to libbz2's exact request sizes, or to what libbz2 actually uses rather than what it documents, could fail. Compression and in-formula decompression stay inside the documented formulas, which are the documented contract; the default decompression layout is the row above. [K §6, M1d, M3d] |
| Encoder diagnostics at `verbosity` ≥ 2 that describe libbz2's sorter, and all detail at ≥ 3 | printed | not printed | They describe internals that no longer exist. |
| Exported private symbols (`BZ2_blockSort`, `BZ2_decompress`, `BZ2_crc32Table`, …) | leak from the shared library | not exported | Not part of the API. A reverse-dependency check precedes the first release (§7). Open decision D3. |

## 3. Performance requirements

Output and memory are fixed by §2, so "better" is speed. The reference is libbz2 1.0.8 built with the same compiler at its own flags, measured single-threaded in the same process on the same streams.

**Requirements**

1. **Never slower.** For every file in the benchmark corpus (`data/README.md`), every level 1–9, both directions, every supported platform and compiler: bz2-ng at its shipping configuration is at least as fast as libbz2.
2. **Small streams.** The same holds for independent streams from 1 kB to 64 kB, each with its own `Init` and `End`.
3. **Call pattern.** With caller buffers of 64 bytes or more, the legacy API runs within 10% of whole-buffer speed in both directions.
4. **No regression.** Every optimisation lands with a benchmark it must not regress; the benchmark suite runs on the reference machines for each release candidate.

**When a change gains in one place and loses in another.** The pairings that count most are clang on AArch64 and gcc on x86-64. A small loss with gcc on AArch64 or with clang on x86-64 is acceptable in exchange for a gain on one of those pairings, and is recorded with its figures. Requirement 1 holds on every pairing regardless.

**Where the library stands**

Measured at level 9 through the legacy API with the shipping configuration (the default decoder layout), MB/s with the multiple of libbz2 built by the same compiler, on the pairing that counts most for each machine. [M3b; the Zen 5 and Granite Rapids columns retaken after M3c, M3c §9; the M4 column after M3e, M3e §8]

| Data | M4 Max, Apple clang enc / dec | Zen 5, gcc enc / dec | Granite Rapids, gcc enc / dec | Graviton4, clang enc / dec |
|---|---|---|---|---|
| MC:Z, 640 kB streams | 71.0 (7.0×) / 460 (3.9×) | 73.8 (8.8×) / 439 (4.8×) | 60.1 (8.9×) / 282 (4.4×) | 52.2 (8.1×) / 239 (3.1×) |
| XA:Z, 1.65 MB streams | 75.3 (3.2×) / 297 (4.1×) | 62.4 (3.1×) / 269 (4.9×) | 50.3 (3.0×) / 181 (4.5×) | 48.2 (2.9×) / 158 (3.3×) |
| Silesia, 12 files | 65.1 (2.9×) / 242 (3.6×) | 55.3 (2.7×) / 213 (4.1×) | 45.0 (2.8×) / 150 (4.1×) | 43.8 (2.8×) / 140 (3.3×) |

Requirement 1 holds in every cell measured, including 16 kB streams, both decoder layouts and both compilers on each server; the lowest multiple is 1.61 (Graviton4, MC:Z in 16 kB streams, decompression). Three things qualify the table. With gcc on x86-64 the library runs at 0.87–0.95 of its clang build for compression and, since M3c, 0.95–1.04 for decompression (it was 0.78–0.97; the one cell at 0.95 is Granite Rapids decoding XA:Z in 16 kB streams, and what remains there is gcc 13's register allocation in the symbol loop, which gcc 16 does better) [M3c]. On Graviton4 large-block decompression has been measured in two states of the machine: on 4 October libbz2 and bz2-ng both decoded XA:Z and Silesia more slowly there, libbz2 by about a third, which put those multiples at 3.9–4.5. The cause is not established; it was not the kernel build, which was the same on both days [M3a §4, M3b §3]. And the legacy API costs the encoder nothing at calls of 5,000 bytes or more [K].

For scale, bzip2 level 9 output on these sets is 20–32% smaller than libdeflate's at gzip level 5, which compresses two to seven times faster than bz2-ng and decompresses five to eleven times faster. [M3b §4]

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
int32_t bzng_bwt(const uint8_t *T, uint8_t *L, int32_t n, int32_t q,
                 int32_t *freq, int32_t *work, size_t work_ints);
```

`T` is the block's least rotation; the function writes the cyclic BWT to `L` (which may alias `T`) and the byte histogram to `freq`, and returns the row of rotation `q`, which is `origPtr`. It allocates nothing and has no global state. The workspace is `n + n/2 + 2048` ints, which is sufficient for any input. [L1 §2]

The sorter is libsais, by Ilya Grebnov, vendored unmodified under `c/third_party/libsais/` at commit `1a696e6` (§8). `lib/sort_libsais.c` compiles it into its own translation unit and fits it to the boundary: [L1]

- **A suffix array, then one pass.** libsais sorts the suffixes of `T` into the start of the workspace. `T` is a Lyndon word, so suffix order is rotation order: one pass writes the byte before each suffix to `L` and finds the row of rotation `q`. A `T` whose least suffix is not suffix 0 is refused.
- **Nothing is allocated.** libbz2's API cannot report an allocation failure from a compress call, and libsais has no allocator hook. Its context is its 2,048 bucket counters, which are placed in the last 2,048 ints of the workspace; the rest of the workspace beyond the suffix array is its free space. During a sort libsais allocates only in its recursion, at a level whose alphabet exceeds both 1,000 symbols and that level's free space, and free space of ⌊n/2⌋ − 2 at the top rules that out for every input of length `n`; the workspace leaves at least ⌊n/2⌋. [L1 §2] Inside the translation unit `malloc` returns NULL and `free` does nothing, so a mistake in that bound would fail a sort instead of taking memory behind the caller's `bzalloc`.
- **No libsais symbol is visible.** Its public functions are declared `static` ahead of its header, so the library defines and exports none of them and cannot clash with an application's own libsais.
- **One compiler flag.** gcc on AArch64 compiles this file with `-fno-sched-spec`. gcc's first scheduling pass, which is on by default there, otherwise moves a prefetch in libsais's final induction scans above the test that guards it; for some entries the address is then far outside the arrays, and Arm cores stall for about 90 cycles on a prefetch of an unmapped address, which costs 17–35% of compression speed. [L3] With gcc on x86-64, where that pass is off by default, the flag changes no byte of the object. A source fix (an unconditional read prefetch of a clamped address, as libsais already does for its first two cursors) removes the stall under every compiler but costs clang on AArch64 about 0.5%; the flag is free, so the library keeps the flag and the vendored file unmodified, and the source fix is offered upstream [L3 §11]. A later pin to a release that carries it would make the flag unnecessary.

libsais replaced a sorter of the project's own, derived from sais-lite 2.4.1, once its relicence made it usable [T, T2]. Compared at level 9 on the four reference machines with gcc and clang, at identical memory, libsais compresses general data (Silesia) and XA:Z 6–30% faster on every pairing; on MC:Z it is between 5% slower and 17% faster on the three servers and 17–23% slower on the M4. [L2]

### 4.4 Decoder

- **Header parser.** libbz2's per-bit resumable structure, reading from a 64-bit buffer. Errors are raised after exactly the bits libbz2 would have read. [J §1]
- **Symbol loop.** A fast path decodes whole 50-symbol groups when at least 160 input bytes are available (table-driven Huffman, the head of the MTF list in a vector register); an exact path handles one symbol at a time near the end of the caller's input. Both give libbz2's results, including for code sets that are not complete prefix codes.
- **Block finishing.** Segmented table build, then a multi-chain inverse-BWT walk into a pool of chunks. In the default layout every block uses 4-byte table entries and a separate symbol array becomes the walk's output pool. In the in-formula layout, blocks above three quarters of the level's block size use 3-byte entries so that the symbol array can become the pool; a mark in each entry's spare bit lets the walk take the output symbol from the guess table without checking it, and smaller blocks use 4-byte entries in the same allocation. The walk does no I/O.
- **Output.** A resumable un-RLE1 with CRC over each call's output, writing straight into the caller's buffer.
- **`small` mode.** libbz2's packed 2.5-byte table and single chain, with the fast symbol loop and a table lookup in place of the binary search. [J §2.4]
- **Tunables fixed by measurement:** 24 chains for blocks of at least 65,536 rows in both layouts (fewer for smaller blocks), software prefetch in walks of 16 or more chains, and four table-build segments on every target. None of them affects output. [P §7, M1d, M3d] Two segments, 1–7% faster than four on Graviton4 in the prototype [P §7], are not faster there in the library with clang (Silesia 2% slower) and gain under 2% in three of ten cells with gcc; on the M4 they are 7–14% slower [M3e].
- **Safety invariants** from the decoder's CVE history are enforced on every path; a fast path never reads or writes outside its buffers on hostile input. [F]

### 4.5 Kernels and run-time dispatch

- Every ISA-specific routine sits behind a dispatch pointer that selects its implementation on first call from one cached mask of the CPU's features (`cpuid`/`xgetbv` on x86, the auxiliary vector read from `/proc/self/auxv` on Linux AArch64, `sysctl` on macOS), as libdeflate does. **The tier is chosen at run time, never at compile time**: one binary carries all tiers for its architecture.
- Each tier is its own translation unit compiled with that tier's flags; the rest of the library uses the distribution's baseline flags.
- Granularity is one call per block per stage; the decoder's symbol loop is instantiated whole per tier so the MTF head can live in a register across symbols.

| Tier | Requires | Role |
|---|---|---|
| Scalar | portable C | Fallback for any CPU, and the reference every tier is tested against |
| x86 SSE4 | x86-64-v2 and PCLMULQDQ | Floor for x86-64; within 1–3% of AVX2 |
| x86 AVX2 | x86-64-v3; VPCLMULQDQ when present | Default on current x86 |
| AArch64 NEON | NEON and CRC32; PMULL for the CRC where present | Default on AArch64 |

There is no AVX-512, SVE2 or SIMDe tier: each measured at 0–3% or less over the tiers above. [P §6, §9] An environment variable forces a tier, and on AArch64 another removes PMULL, for tests and measurement only.

CRC kernels: slice-by-16 (scalar); on AArch64 carry-less folding with PMULL where the CPU has it, the `crc32` instructions alone otherwise; carry-less multiplication folding on x86. [M3e]

### 4.6 Determinism

All stages are exact integer computations. Tests require every tier available on the test machine to produce identical bytes, in both directions.

### 4.7 Portability

C11, no compiler extensions without a fallback. Little- and big-endian, 32- and 64-bit: the suite passes under emulation on s390x (64-bit big-endian), i386, armv7 and mips (32-bit; mips is big-endian and traps misaligned word accesses), and every target gives the same bytes [M2a]. gcc, clang and MSVC; libsais (§4.3) compiles with those and Intel's compiler and stops with an error on any other. A compress call uses up to about 23 kB of stack, 16 kB of it one buffer inside libsais (measured with gcc 16 on AArch64) [L1 §14]. Platforms without a vector tier run the scalar tier, as does big-endian AArch64, since the NEON tier assumes little-endian lane order. x32 (x86-64 with 32-bit pointers) builds the scalar tier only; a selection of the suite passes for it under full-system emulation, and it has not run on hardware [M2e]. Windows on x86-64 builds with MSVC (cl in C11 mode, without warnings at `/W4`) and passes CI's selection of the suite on Windows itself, on every x86 tier and with the same bytes. Under cl the SSE4 tier is compiled from intrinsics alone and the AVX2 tier under `/arch:AVX2`; under every compiler, a tier file built without its instruction set fails to compile instead of building portable code under the tier's name [M2f]. Windows on x86-64 and i686 also builds with mingw-w64 and passes the suite under Wine, and a program built against stock libbz2's DLL runs unchanged with bz2-ng's [M2c]. MSVC's speed is not measured; Windows on ARM64 is out of scope. They must still meet §3, which has not yet been measured on any of them.

### 4.8 Code style

The library follows libdeflate's conventions, so that it reads like one well-known, respected codebase and can be reviewed against it: tabs at width 8, 80-column lines, kernel-style braces, one `common_defs.h` for compiler and platform abstractions, CPU feature detection and kernels in `lib/x86/` and `lib/arm/` with a small selector header per kernel family, dispatch pointers that replace themselves on first call, and template headers for per-tier instantiations. Files that are libbz2's own code, unmodified or lightly altered, keep upstream's formatting so they diff cleanly against bzip2 1.0.8, and the vendored libsais is upstream's file, never reformatted.

### 4.9 Extended API

Deferred until after the drop-in is complete (§7, M5). It adds no speed; its value is reusable contexts with input-sized memory, and options the legacy API has no slot for. The one ratio option planned is a guard that codes each block both libbz2's way and an alternative way and emits the smaller, which is never larger than libbz2 by construction and gains 0.13–0.37% for 5–9% more encode time. [H3] Everything in the extended API lives under a new symbol prefix and a separate header.

## 5. Verification

- **Differential conformance.** A tester links stock libbz2 1.0.8 under renamed symbols beside bz2-ng and drives both with identical call sequences, comparing return code, stream pointers, totals and bytes after every call. Compression: every split point for small inputs, random `RUN`/`FLUSH`/`FINISH` sequences, output space down to zero, inputs around block capacity, every level. Decompression: every input and output split for small streams, both `small` settings, mutated and truncated streams, concatenated streams through `BZ2_bzRead`, allocation failures. [J §4, K §5] A generated input whose Huffman codes exceed libbz2's 17-bit limit covers the code-length limiting, which ordinary inputs seldom reach and no other differential test does. [M2d]
- **Cross-decoding.** Everything bz2-ng writes is decoded by stock libbz2, and the reverse.
- **Sorter.** Output compared with an independent oracle (cyclic prefix doubling, itself checked against a naive rotation sort) on generated families: exhaustive small alphabets, every short length, Fibonacci and Thue–Morse words, de Bruijn sequences, long runs, periodic and near-periodic blocks, worst-case inputs for the workspace bound, random lengths to the maximum block. [T §4] The same families run over a build of the sorter whose `malloc` aborts, so that an input for which libsais wanted memory would fail the test; and the static and shared libraries are checked to define and export no libsais symbol and to reference no allocator from the sorter's object. [L1 §14]
- **Every tier.** All of the above run for each kernel tier the machine supports, and tiers are compared with each other.
- **Byte order and word size.** The suite runs on a big-endian and a 32-bit target under emulation, and the same generated inputs give the same bytes there as on the reference machines. [M2a] On 32-bit targets a test decodes from one input buffer longer than `PTRDIFF_MAX`, call for call against libbz2. [M2e]
- **Sanitisers and fuzzing.** The suites run under AddressSanitizer and UndefinedBehaviorSanitizer. Fuzz targets for the decoder, the encoder and the sorter (`c/fuzz/`, built with `BZ2NG_BUILD_FUZZ`) run differentially against libbz2 or the oracle, on every tier of the machine; each has been shown to find faults planted in the library. [M2b]
- **Memory.** Counting-allocator tests against the formulas of §2.4 at every level.
- **Drop-in tests.** bzip2's own `make test`; the `bzip2` program built against bz2-ng compared with stock bzip2 across option combinations (stdout, stderr, exit status); CPython's `test_bz2` with the library preloaded; further downstream suites before release (§7, M4).
- **ABI.** The exported symbol list is checked against the 24, on Windows from the DLL's export table; `bz_stream` layout is checked by a compile-time test.
- **Code review.** Library changes are reviewed by independent reviewers against this specification and the house style (§4.8) before they merge; the differential testers, not review, are the evidence of correctness.
- **Tests generate their data.** The corpora are for benchmarks and bulk checks; unit and conformance tests build their inputs in code.

## 6. Build and continuous integration

- CMake is the build system, producing a static and a shared library, the `bzip2` and `bzip2recover` programs from libbz2's unmodified sources, and `bzip2.pc`.
- The build stays simple enough for another build system to reproduce from a short description, because the bindings compile the C sources directly: a flat list of sources, per-tier compiler flags confined to the tier translation units, no generated code, and CPU features detected at run time rather than at configure time.
- CI builds and tests with **both gcc and clang** on Linux x86-64 and AArch64, with sanitiser jobs, a `BZ_NO_STDIO` job, an emulated big-endian job (s390x, cross-compiled, its tests under QEMU), a 32-bit job (i686, run natively on the x86-64 runner), a job that runs each fuzz target briefly from its generated seeds, a Windows job (x86-64, cross-compiled with mingw-w64, its tests under Wine), and an MSVC job (x86-64, built by the Windows runner's Visual Studio and tested there, failing on any compiler or linker warning). macOS is covered by local runs, not hosted runners. CI runs when started deliberately at milestones and on merges to `main`, not on every push.
- CI does not repeat the whole suite. It leaves out the mutation tests; its sanitiser jobs run one vector tier without the capacity tests; the big-endian job leaves out the tests that take more than five minutes under emulation; and the Windows job runs the heaviest decoder and capacity phases on the AVX2 tier only. A run of the twelve jobs other than the Windows one costs about 125 minutes of runner time (measured: nine to twelve minutes for each AArch64 job, 16 to 22 for each full x86-64 job, eleven and thirteen for the 32-bit and big-endian jobs, and six for each fuzz job). The Windows job takes 17 minutes on a hosted runner (measured: 58 tests), and the MSVC job 18 to 33 minutes on a Windows runner (measured twice with the same 68 tests: the hosted Windows runners vary that much), which Windows bills at twice the Linux rate; a full run is about 165 to 180 minutes of runner time, or about 180 to 215 at the Linux rate. The whole suite runs on the reference machines: as built for release for every change to the library, and under the sanitisers at each milestone; it runs under emulation for a big-endian and a 32-bit target, and the long fuzzing campaigns run, at milestones too. The big-endian job is the only one that can see a byte-order mistake, since on a little-endian target every byte swap compiles away [M2a §10].
- A performance job on fixed reference machines compares against libbz2 for release candidates, for gcc and clang builds: the distribution's default version of each and the latest release of each.

## 7. Milestones

| | Milestone | Done when |
|---|---|---|
| M1 (done) | **Integration.** One library containing the encoder driver, the decoder driver, the block sorter, and the scalar, SSE4, AVX2 and NEON tiers with run-time dispatch, over libbz2's stdio layer. | Both differential testers pass on every tier; bzip2's `make test` and CPython's `test_bz2` pass; memory is inside every formula, measured; speed within 3% of the prototypes at the shipping configuration on the four reference machines. |
| M2 (done) | **Hardening.** Fuzz targets, the CI matrix of §6, export list and ABI checks, `BZ_NO_STDIO`, Windows, big-endian and 32-bit. | CI green on the full matrix; a fuzzing campaign of agreed length finds nothing. |
| M3 | **Performance.** In the library already, for the decoder: mask arithmetic in the symbol loop under gcc, 24 walk chains in the default layout and walk prefetch [M1d]; a table build that under clang loads the four cursors of an iteration before any table store and writes each table entry before its cursor [M1f, M1g]; and, under gcc, a run flag that no partial-register write can chain to the previous symbol's vector result, a Huffman table fill of four entries per store, and the code length by `bzhi` where BMI2 is enabled [M3c]; for the in-formula decoder, a guess mark in the 3-byte entries and 24 chains in the guess table's shared space, which also return the default layout to 100 kB + 5 × block size [M3d, which closes the layout decision of §2.4]; on AArch64, a PMULL-folding CRC (decode 1–3.5% faster on the M4, unchanged on Graviton4) [M3e]. Measured in the library and not taken: two table-build segments on AArch64 cores other than Apple's [M3e]. Still to do: a wider MTF head across several registers. | Requirement 1 of §3 met with gcc and clang on every reference machine; each change carries its benchmark. |
| M4 | **Drop-in validation.** Rebuild-free substitution on a real distribution: the test suites of a chosen set of dependants run against bz2-ng; packaging names; a porting note listing §2.5. | The chosen suites pass; private-symbol reverse-dependency check done. |
| M5 | **Extended API and bindings.** The extended API; a Rust `-sys` crate with a safe wrapper in `rust/`; a JNI library in `java/`. | Reusable contexts and the guarded ratio mode available under the new prefix; both bindings build from the sources in `c/` and pass their own round-trip and cross-decoding tests; documented. |

**M1 outcome.** The library is in `c/` [M1]. Conformance, drop-in tests, memory and "never slower" are met on all four reference machines with gcc and clang. Speed is within 3% of the prototypes on the M4 and for Graviton4 encode; x86 encode is 3–5% under the projection of §3, and four server decode cells are at 0.83–0.92 of the one-shot prototype, which M3 takes up (walk chains, prefetch, and the gcc-specific loop shapes of report P2).

**Since M1.** The cross-review's findings are fixed [M1b]. The decoder's symbol loop under gcc, 24 walk chains and walk prefetch are in: level 9 decompression of full-size blocks in the default layout is 7–21% faster on the three servers with gcc and clang and about 1% faster on the M4, with 16 KiB streams unchanged to 13% faster [M1d]. The slowest of the four M1 decode cells (Granite Rapids, MC:Z, clang) is traced to the table build and recovered. Writing the table entry before the cursor gains about 8% there and costs nothing elsewhere [M1f]. Loading all four cursors before any table store gains a further 14–16% there and ends a slow mode on small streams; it is neutral on the M4 and Graviton4, costs 0.4–1.2% on MC:Z with clang on Zen 5, and leaves gcc builds unchanged [M1g]. Big-endian and 32-bit targets build and pass the suite (s390x, i386, armv7 and mips under emulation), with the objects of little-endian 64-bit builds unchanged [M2a]. The fuzz targets exist, and a first campaign of an hour per target on AArch64 (0.7 to 0.9 million executions each) found nothing, and neither did four hours per target on Zen 5, over every x86-64 tier, and on Graviton4 (1.2 to 3.2 million executions per target and machine) [M2b §13]. The Huffman code-length limit, which no differential test reached, has a test of its own [M2d]. On 32-bit targets the decoder no longer forms pointer differences past `PTRDIFF_MAX`, and x32 builds [M2e]. Windows builds with mingw-w64 and passes the suite under Wine on x86-64, every tier included; the DLL exports exactly the 24 functions, undecorated, and replaces stock libbz2's DLL without relinking [M2c]. MSVC builds the library, the programs, the tests and the benchmark on x86-64 without warnings at `/W4`, and CI's selection passes on Windows on every x86 tier; a tier built without its instruction set no longer compiles [M2f]. The block sorter is libsais in place of the project's own, which M1 integrated (§4.3) [L1, L2, L3]; a fuzzing campaign on that library, 24 CPU-hours per target on Zen 5 and on Graviton4 over every tier (3.7 to 8.0 million executions per target and machine), found nothing [M2b §14].

**M2 outcome.** The CI matrix of §6 runs green, MSVC included (run 37480154236, 6 October: 14 jobs, 146 Linux and 33 Windows runner minutes); the fuzzing campaigns found nothing [M2a–M2f]. Left to M4: the Windows package's DLL name and C runtime. Not done: Windows on ARM64 (out of scope) and clang-cl.

**Before any public release:** licence statements for the benchmark data are verified, and SIMDe and other spike-only code are confirmed absent from the library.

## 8. Licensing and provenance

- The library's own code is MIT. Files derived from bzip2 1.0.8 (the drivers and the stdio layer) keep bzip2's BSD-style licence and are marked as altered, as that licence requires. libsais is Apache-2.0 WITH LLVM-exception; its licence text is beside the vendored source and in `c/licenses/`.
- Code under GPL, LGPL or plain Apache-2.0 is never copied or translated. libsais is used from the commit at which its author relicensed it to Apache-2.0 WITH LLVM-exception (`1a696e6`, 5 October 2026); the exception removes the conflict that plain Apache-2.0 has with GPLv2-only programs that link a system libbz2. Only the vendored copy is read or used; it is unmodified, and an altered file would say so in its first lines. Its releases up to 2.10.4, and the copies in the spikes, carry plain Apache-2.0: nothing is taken from them.
- `THIRD_PARTY.md` lists third-party code in the repository. Nothing listed there as spike-only ships.

## 9. Open decisions

| | Decision | Recommendation |
|---|---|---|
| D2 | **Calls after a decompression error.** Return the same error again, or emulate libbz2's resumption. | Return the same error. |
| D3 | **Private symbols.** Export only the 24 public functions, or keep exporting libbz2's leaked internals as stubs. | Only the 24, after the reverse-dependency check in M4. |
| D6 | **Programs.** Ship `bzip2` and `bzip2recover` built from libbz2's unmodified sources. | Yes: a distribution replaces the package, not only the library, and the tests need them. |