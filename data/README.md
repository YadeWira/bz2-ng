# Benchmark data

Two kinds of data live here: tag and quality corpora regenerated from public 1000 Genomes CRAMs by one script (`make-1kg-corpora.sh`, committed via Git LFS), and a general-purpose corpus fetched by another (`fetch-general-corpora.sh`, never committed).

| Item | What it is | Committed |
| --- | --- | --- |
| `corpora/1kg/<SAMPLE>/` | MC, MD, XA, SA tag values and QUAL strings from two 1000 Genomes samples, plus CRAM-slice sidecars | Yes, via Git LFS |
| `silesia/` | The 12-file Silesia corpus | No, fetched |
| `general/` | Canterbury Large (3 files), `enwik8`, a source-code tar, a log file and a JSON file | No, fetched |

`mc.bin` and `xa.bin` are older private extracts that predate these corpora; they are not described here and are not part of the published set. `silesia/` and `general/` are listed in `.gitignore`.

## 1000 Genomes tag and quality corpora

Regenerate with `./make-1kg-corpora.sh` (about 15 seconds; needs `samtools` and `python3`, standard library only). It reads the NYGC high-coverage CRAMs and writes `corpora/1kg/`. The inputs and output location can be overridden with the environment variables `CRAM_DIR`, `REFERENCE` and `OUT_DIR`; the samples, regions and size targets are fixed in the script. The output is deterministic: the same inputs give identical bytes, and `corpora/1kg/SHA256SUMS` covers every output file (`cd corpora/1kg && shasum -a 256 -c SHA256SUMS`, or `sha256sum -c`).

### Source and licence

The CRAMs are the NYGC 30x high-coverage re-sequencing of the 1000 Genomes samples (Illumina NovaSeq 6000, 150 bp paired-end, BWA-MEM alignment to GRCh38 with decoys and HLA contigs, base qualities recalibrated and binned to the Illumina 8-bin scheme), downloaded from the public `1000genomes` S3 bucket. The IGSR publishes 1000 Genomes data as open data with no use restrictions; please cite Byrska-Bishop et al., "High-coverage whole-genome sequencing of the expanded 1000 Genomes Project cohort including 602 trios", Cell 2022. The files here contain only tag values and quality strings, no sequence, read names or genotypes.

### Samples and regions

The two samples never share a chromosome, so tuning on the training sample can be confirmed on data from a different individual and different regions. Every region starts at position 1,000,001 to skip the telomere, where reads are dominated by subtelomeric multi-mapping. Chromosomes 21 and 22 are not used: they are acrocentric, and their short arms and pericentromeres are satellite-dense (on chr21, over half of the first three million records carry an XA tag), which makes them unrepresentative.

| Role | Sample | Region scanned in the published run | Records scanned |
| --- | --- | --- | --- |
| Training | NA12878 (CEU) | `chr20:1000001-` up to chr20:30,091,826 | 8,100,000 |
| Held-out | NA19141 (YRI) | `chr19:1000001-` up to chr19:48,210,445 | 10,500,000 |

Scanning proceeds through a fixed list of regions and stops as soon as every series has reached its target. Training would continue with `chr18`, `chr17`, `chr16`, `chr12`, `chr10`, `chr8`, `chr6`, `chr4` and `chr2`, held-out with `chr15`, `chr14`, `chr13`, `chr11`, `chr9`, `chr7`, `chr5`, `chr3` and `chr1` (each from position 1,000,001), but neither needed more than its first region. The scan is also capped at 150 million records per sample. The regions actually scanned, with their record counts and the position of the last record read, are in each sample's `scan-summary.tsv`.

### Files

Each sample directory `corpora/1kg/<SAMPLE>/` holds:

| File | Content |
| --- | --- |
| `MC.bin`, `MD.bin`, `XA.bin`, `SA.bin` | The value of that `:Z` tag (value only, without the `MC:Z:` prefix) for every record that carries it, in file order, each value followed by one NUL byte (`0x00`), concatenated |
| `QUAL.bin` | The QUAL column of every record, in file order, each followed by one newline (`\n`), concatenated |
| `<NAME>.slices-10k.txt`, `<NAME>.slices-100k.txt` | Slice sidecars for each `.bin`, described below |
| `scan-summary.tsv` | Tab-separated `kind`, `key`, `value` lines: records scanned, stop reason, per-series values, bytes, target and records covered, per-region record counts and the last record's position |

Records are everything `samtools view` returns for the region, with no flag filtering (primary, secondary, supplementary, duplicate and QC-fail reads all included), so each file is the stream a CRAM writer would see. Values are verbatim from `samtools view` text. `QUAL.bin` stores the QUAL column as printed, so a record without qualities would appear as `*`; none occur in these samples, and every QUAL line is 150 characters.

MD (and NM) are not stored in these CRAMs; htslib regenerates them from the reference while decoding, which is also what a reader of the CRAM gets. The corpora were generated with samtools 1.24.

### Slice sidecars

A CRAM slice holds a fixed number of consecutive records. `<NAME>.slices-10k.txt` and `<NAME>.slices-100k.txt` assume slices of 10,000 and 100,000 records. Slice membership is counted over the whole record stream, so every record counts towards its slice whether or not it carries the tag, and the count does not reset between regions. Each sidecar has one decimal integer per line: the cumulative end byte offset, in the `.bin`, of that slice's values. Slices that contribute zero bytes to the `.bin` are omitted, so the offsets are strictly increasing and the last offset equals the file size. Every 100k boundary also appears among the 10k offsets.

To cut a `.bin` into the blocks a CRAM writer would compress:

```python
data = open("XA.bin", "rb").read()
start = 0
for line in open("XA.slices-10k.txt"):
    end = int(line)
    block = data[start:end]
    start = end
```

For instance, the first lines of NA12878's `MC.slices-10k.txt` are `51329`, `103269` and `154988`: the first slice's values occupy bytes 0 to 51,328 of `MC.bin`, the second bytes 51,329 to 103,268, and so on.

### Sizes

Each series stops growing at the first 100,000-record slice boundary at which it holds at least its target (MC, MD, XA and QUAL 16 MB, SA 4 MB, decimal megabytes), so a series can overshoot by up to one 100,000-record slice. QUAL overshoots most: one 100,000-record slice of 150-character qualities is 15.1 MB, so the 16 MB target is met by two slices (30.2 MB). XA and SA are sparse, so they fill more slowly than MC and MD, and SA, the sparsest, sets how many records are scanned. "Records covered" is how many records the series was accumulated over, which is what to divide by to get the fraction of records carrying the tag.

| Sample | Series | Values | Bytes | Records covered |
| --- | --- | --- | --- | --- |
| NA12878 | MC | 3,096,315 | 16,062,465 | 3,100,000 |
| NA12878 | MD | 3,196,296 | 16,517,087 | 3,200,000 |
| NA12878 | XA | 37,464 | 16,497,491 | 1,900,000 |
| NA12878 | SA | 101,761 | 4,295,951 | 8,100,000 |
| NA12878 | QUAL | 200,000 | 30,200,000 | 200,000 |
| NA19141 | MC | 2,993,974 | 16,107,447 | 3,000,000 |
| NA19141 | MD | 2,694,636 | 16,342,174 | 2,700,000 |
| NA19141 | XA | 74,030 | 16,939,978 | 1,900,000 |
| NA19141 | SA | 100,534 | 4,014,150 | 10,500,000 |
| NA19141 | QUAL | 200,000 | 30,200,000 | 200,000 |

Block sizes are very uneven for XA and SA, unlike MC, MD and QUAL. The largest 10,000-record XA block is about 40 times the median in NA12878 and 6 times in NA19141; for SA it is about 260 times and 75 times. In NA12878 about 72% of the SA bytes come from the last 1.1 million records, chr20:27.7 to 30.1 Mb, which lies inside the GRCh38 centromere (cytoband `acen` bands p11.1 and q11.1 span 25.7 to 30.4 Mb; the modelled centromere sequence spans 26.4 to 30.0 Mb).

## General corpus

Run `./fetch-general-corpora.sh` to download everything (about 770 MB on disk, 570 MB of it in `general/`, in under a minute on a fast link). Each item is pinned by the SHA-256 of the final file in the script; an item already present with the right hash is skipped, and a download whose hash differs fails the script and leaves nothing behind. Nothing in `silesia/` or `general/` is committed, and the script is the only record of what they contain.

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
