# Third-party code

Code in this repository that was not written for bz2-ng, where it lives, and what its licence requires. Each copy keeps its upstream `LICENSE` file beside it.

## In the library (`c/`)

This code ships. Licence texts are in `c/licenses/`; files taken or derived from another project keep its notice, and altered files say so in their first lines.

| Component | Version | Licence | Location | Notes |
|---|---|---|---|---|
| bzip2 / libbzip2 | 1.0.8 | BSD-style (Julian Seward) | `c/bzlib.h`, `c/programs/`, `c/test/reference/` | Unmodified: the public header, the `bzip2` and `bzip2recover` programs, and the reference library and sample files the tests compare against. |
| bzip2 / libbzip2, altered | 1.0.8 | BSD-style (Julian Seward) | `c/lib/bzlib.c`, `bzlib_private.h`, `randtable.c`, `compress.c`, `decompress.c`, `enc_huff.c`; `c/test/rand_compress.c` | The stdio and buffer-to-buffer layer, the two legacy drivers (libbz2's state machines) and the Huffman length and code assignment. |
| sais-lite, altered | 2.4.1 | MIT (Yuta Mori) | `c/lib/sort.c`, `sort_template.h`, `sort.h` | The block sorter. |
| libdeflate | 1.26 | MIT (Eric Biggers) | `c/lib/common_defs.h`, `cpu_features_common.h`, `c/lib/x86/cpu_features.[ch]`, `c/lib/arm/cpu_features.[ch]` | Compiler and platform abstractions and CPU feature detection. |

## In the spikes only

None of this ships.

| Component | Version | Licence | Location | Notes |
|---|---|---|---|---|
| bzip2 / libbzip2 | 1.0.8 | BSD-style (Julian Seward) | `spikes/*/vendor/bzip2/` | Unmodified reference copies, used as the baseline and as the test oracle. |
| bzip2 / libbzip2, altered | 1.0.8 | BSD-style (Julian Seward) | `spikes/*/bz_instr/`, `spikes/enc-huffman/bz_huf/`, `spikes/decoder-spike/bz_rand/`, `spikes/enc-standalone/swap/{blocksort,bzlib,compress}.c` | Altered copies (stage timers, replaced sorter, reworked Huffman stage). The licence requires altered versions to be plainly marked; each file says so in its first lines. |
| libdivsufsort | 2.0.2 | MIT (Yuta Mori) | `spikes/c-sorter-spike/vendor/libdivsufsort/`, `spikes/enc-huffman/vendor/libdivsufsort/` | Comparison sorter in the sorter benchmarks. |
| sais-lite | 2.4.1 | MIT (Yuta Mori) | `spikes/own-sorter/vendor/sais-lite-2.4.1/` (unmodified); `spikes/own-sorter/src/bzs_*.c`, `bzs_tmpl.h` (altered) | Starting point of the project's own block sorter. The altered files keep the MIT notice and are marked as altered. Fetched from two byte-identical mirrors because the original distribution is no longer online. |
| SIMDe | 0.8.2 | MIT | `spikes/portability/vendor/simde/` | Compiles the NEON kernels for x86 in the portability spike, to compare against native x86 kernels. Not intended for the library. |
| libdeflate | 1.26 | MIT (Eric Biggers) | `spikes/portability/vendor/libdeflate/` | Reference codec in the benchmark tables (levels 1 and 5). (A few of its support files are also used in the library; see above.) |
| libsais | 2.10.4 | Apache-2.0 (Ilya Grebnov) | `spikes/*/vendor/libsais/` | The sorter behind the spike measurements. **Spikes only: it must not ship in the library.** Apache-2.0 is incompatible with GPLv2-only programs that link a system libbz2, so the library needs a sorter under a permissive licence without that conflict. |

Sources that may be consulted for ideas but never copied or translated: lbzip2 (GPLv3), 7-Zip's bzip2 encoder (LGPL), and libsais's implementation techniques.
