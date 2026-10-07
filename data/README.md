# Benchmark data

Two kinds of data live here: a general-purpose corpus fetched by `fetch-general-corpora.sh` (never committed), and a set of short-string corpora drawn from public 1000 Genomes sequencing data by `make-1kg-corpora.sh` (committed via Git LFS).

| Item | What it is | Committed |
| --- | --- | --- |
| `silesia/` | The 12-file Silesia corpus | No, fetched |
| `general/` | Canterbury Large (3 files), `enwik8`, a source-code tar, a log file and a JSON file | No, fetched |
| `corpora/1kg/<SAMPLE>/` | MC, MD, XA, SA tag values and QUAL strings from two 1000 Genomes samples, with slice sidecars | Yes, via Git LFS |

`c/scripts/bench_run.sh` and the tables in `docs/spec.md` §3 use Silesia and NA12878's `MC.bin` and `XA.bin`.

## General corpus

Run `./fetch-general-corpora.sh` to download everything (about 770 MB on disk, 570 MB of it in `general/`, in under a minute on a fast link). Each item is pinned by the SHA-256 of the final file in the script; an item already present with the right hash is skipped, and a download whose hash differs fails the script and leaves nothing behind. Nothing in `silesia/` or `general/` is committed (both are in `.gitignore`), and the script is the only record of what they contain.

| Item | What it is | Size (bytes) | Source | Licence and redistribution |
| --- | --- | --- | --- | --- |
| `silesia/` (12 files) | The Silesia corpus: text, source, executables, a database, images and XML | 211,938,580 in total | `https://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip`; the mirror `https://mattmahoney.net/dc/silesia.zip` has identical members | Published for compression benchmarking by Sebastian Deorowicz. Several members carry their own licences (for example `samba` is GPL source), which have not been audited here, so it is fetched, not committed |
| `general/E.coli` | Canterbury Large: 4.6 MB of *E. coli* DNA as plain letters | 4,638,690 | `https://www.data-compression.info/files/corpora/largecanterburycorpus.zip` (the canonical `corpus.canterbury.ac.nz` answers scripted requests with 403) | Canterbury Corpus (Arnold and Bell, 1997), published for compression benchmarking; terms not checked, so it is fetched, not committed |
| `general/bible.txt` | Canterbury Large: the King James Bible | 4,047,392 | as above | as above |
| `general/world192.txt` | Canterbury Large: the CIA World Factbook text | 2,473,400 | as above | as above |
| `general/enwik8` | The first 10^8 bytes of an English Wikipedia XML dump (Hutter Prize data) | 100,000,000 | `https://mattmahoney.net/dc/enwik8.zip` | Wikipedia text, CC BY-SA |
| `general/Python-3.12.7.tar` | CPython 3.12.7 source: mostly Python and C, plus documentation and test data, as an uncompressed tar | 104,734,720 | `https://www.python.org/ftp/python/3.12.7/Python-3.12.7.tgz`, decompressed with `gzip -d` | Python Software Foundation licence |
| `general/Android.log` | A 1.55-million-line Android logcat capture from the Loghub collection | 192,270,829 | `Android.tar.gz` in Zenodo record 3227177 (`https://zenodo.org/api/records/3227177/files/Android.tar.gz/content`) | Zenodo metadata says CC BY 4.0; the Loghub README says the datasets are free for research or academic work and asks users to cite the Loghub paper |
| `general/citylots.json` | San Francisco city-lot parcels as one 190 MB GeoJSON FeatureCollection (206,560 features, long coordinate arrays) | 189,778,220 | `https://raw.githubusercontent.com/zemirco/sf-city-lots-json/33c27c137784a96d0fbd7f329dceda6cc7f49fa3/citylots.json`, pinned to the only commit that touches the file | The repository's README carries an MIT notice for the conversion; the licence of the underlying San Francisco open data has not been verified (believed to be the Open Data Commons PDDL) |

## 1000 Genomes tag and quality corpora

Streams of short, highly structured strings compress differently from text and binaries, and are often compressed in small blocks; sequencing-alignment tags and quality strings, as a CRAM writer sees them, are a convenient public source of such data. Regenerate with `./make-1kg-corpora.sh` (about 15 seconds; needs `samtools` and `python3`, standard library only). It reads the NYGC high-coverage CRAMs and writes `corpora/1kg/`; `CRAM_DIR`, `REFERENCE` and `OUT_DIR` override the inputs and output location, while the samples, regions and size targets are fixed in the script. The output is deterministic, and `corpora/1kg/SHA256SUMS` covers every file (`cd corpora/1kg && shasum -a 256 -c SHA256SUMS`, or `sha256sum -c`).

**Source and licence.** The CRAMs are the NYGC 30x high-coverage re-sequencing of the 1000 Genomes samples (Illumina NovaSeq 6000, 150 bp paired-end, BWA-MEM alignment to GRCh38 with decoys and HLA contigs, base qualities recalibrated and binned to the Illumina 8-bin scheme), downloaded from the public `1000genomes` S3 bucket. The IGSR publishes 1000 Genomes data as open data with no use restrictions; please cite Byrska-Bishop et al., "High-coverage whole-genome sequencing of the expanded 1000 Genomes Project cohort including 602 trios", Cell 2022. The files here contain only tag values and quality strings, no sequence, read names or genotypes.

**Samples.** NA12878 (CEU, from chr20) is the training sample and NA19141 (YRI, from chr19) the held-out one, so tuning on one can be confirmed on a different individual and different regions. Regions start at position 1,000,001 to skip the telomere, and chromosomes 21 and 22 are not used because their satellite-dense short arms are unrepresentative. Each sample's `scan-summary.tsv` records the regions actually scanned, their record counts, and each series' value count and byte size.

**Files.** In each `corpora/1kg/<SAMPLE>/`, `MC.bin`, `MD.bin`, `XA.bin` and `SA.bin` hold that `:Z` tag's value (without the `MC:Z:` prefix) for every record that carries it, in file order, each followed by one NUL byte; `QUAL.bin` holds every record's QUAL column, each followed by one newline. Records are everything `samtools view` returns for the region with no flag filtering, so each file is the stream a CRAM writer would see, and values are verbatim. Each series stops growing at the first 100,000-record slice boundary at which it holds its target (16 MB for MC, MD, XA and QUAL, 4 MB for SA; QUAL overshoots to 30.2 MB because one slice of 150-character qualities is 15.1 MB). MD is not stored in these CRAMs; htslib regenerates it from the reference while decoding, which is also what a reader of the CRAM gets. Generated with samtools 1.24.

**Slice sidecars.** A CRAM slice holds a fixed number of consecutive records. `<NAME>.slices-10k.txt` and `<NAME>.slices-100k.txt` give, one decimal integer per line, the cumulative end byte offset in the `.bin` of each 10,000- or 100,000-record slice's values, counted over the whole record stream; slices that contribute no bytes are omitted, so the offsets are strictly increasing and the last equals the file size. Cutting the `.bin` at those offsets yields the blocks a CRAM writer would compress. Block sizes are very uneven for XA and SA, unlike MC, MD and QUAL: the largest 10,000-record XA block is about 40 times the median in NA12878.
