#!/bin/sh
# Builds the 1000 Genomes tag and quality corpora used to benchmark bz2-ng.
#
# For each of two samples (a training sample and a held-out sample drawn from different
# chromosomes) this extracts, from the NYGC high-coverage CRAMs, the values of the MC, MD,
# XA and SA :Z tags (NUL-terminated) and the QUAL column (newline-terminated), plus
# sidecar files giving the cumulative end offset of every 10,000- and 100,000-record
# CRAM slice, so a harness can cut each file into the blocks a CRAM writer would compress.
# See data/README.md for the exact formats.
#
# Usage: make-1kg-corpora.sh
#
# Environment overrides (everything else is fixed so the corpora are reproducible):
#   CRAM_DIR   directory holding <SAMPLE>.final.cram and .crai
#   REFERENCE  GRCh38_full_analysis_set_plus_decoy_hla FASTA (bgzipped, indexed)
#   OUT_DIR    output directory (default: corpora/1kg next to this script)
#
# Requires samtools and python3 (standard library only). The scan runs the two samples one
# after the other and uses 4 samtools threads.

set -eu
LC_ALL=C
export LC_ALL

script_dir=$(cd "$(dirname "$0")" && pwd)
CRAM_DIR=${CRAM_DIR:-/Volumes/DataLibrary/sequence-data/wgs/1kg}
REFERENCE=${REFERENCE:-/Volumes/DataLibrary/reference-genomes/GRCh38_full_analysis_set_plus_decoy_hla/GRCh38_full_analysis_set_plus_decoy_hla.fa.gz}
OUT_DIR=${OUT_DIR:-$script_dir/corpora/1kg}

SAMTOOLS_THREADS=4

# Samples and the chromosomes each may scan, in scan order, every one from SCAN_START
# to its end. The lists must stay disjoint so the held-out sample never shares a chromosome
# with the training sample; this is asserted below. Scanning stops as soon as every series
# is full, which may be before the first listed chromosome is finished.
#
# Starting at 1,000,001 skips the telomere, where the reads are dominated by subtelomeric
# multi-mapping. The acrocentric short arms and pericentromeres of chr13, chr14, chr15, chr21
# and chr22 are satellite-dense and unrepresentative, so chr21 and chr22 are never used. If
# the held-out scan ever falls through to chr15, chr14 or chr13, check its region_records
# in scan-summary.tsv before trusting the result.
SCAN_START=1000001
TRAINING_SAMPLE=NA12878
TRAINING_CHROMOSOMES="chr20 chr18 chr17 chr16 chr12 chr10 chr8 chr6 chr4 chr2"
HELDOUT_SAMPLE=NA19141
HELDOUT_CHROMOSOMES="chr19 chr15 chr14 chr13 chr11 chr9 chr7 chr5 chr3 chr1"

# Hard limit on records read per sample, whether or not every series has reached its target.
RECORD_CAP=150000000

# Per-series size targets in bytes (decimal megabytes). A series stops growing at the first
# 100,000-record boundary at which it holds at least this many bytes, so it can overshoot
# by up to one 100,000-record slice (about 15 MB for QUAL).
MC_TARGET_BYTES=16000000
MD_TARGET_BYTES=16000000
XA_TARGET_BYTES=16000000
SA_TARGET_BYTES=4000000
QUAL_TARGET_BYTES=16000000

SERIES_NAMES="MC MD XA SA QUAL"

die() {
    echo "make-1kg-corpora.sh: error: $*" >&2
    exit 1
}

for tool in samtools python3; do
    command -v "$tool" >/dev/null 2>&1 || die "$tool not found on PATH"
done
if command -v sha256sum >/dev/null 2>&1; then
    sha256_of_files() { sha256sum "$@"; }
elif command -v shasum >/dev/null 2>&1; then
    sha256_of_files() { shasum -a 256 "$@"; }
else
    die "neither sha256sum nor shasum found on PATH"
fi

[ -r "$REFERENCE" ] || die "reference not readable: $REFERENCE"
for sample in $TRAINING_SAMPLE $HELDOUT_SAMPLE; do
    [ -r "$CRAM_DIR/$sample.final.cram" ] || die "CRAM not readable: $CRAM_DIR/$sample.final.cram"
    [ -r "$CRAM_DIR/$sample.final.cram.crai" ] || die "CRAM index not readable: $CRAM_DIR/$sample.final.cram.crai"
done

for chromosome in $TRAINING_CHROMOSOMES; do
    case " $HELDOUT_CHROMOSOMES " in
        *" $chromosome "*) die "$chromosome is in both the training and held-out chromosome lists" ;;
    esac
done

tmp=$(mktemp -d "${TMPDIR:-/tmp}/make-1kg-corpora.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
trap 'exit 1' HUP INT TERM

# The scan helper is embedded so the corpus definition lives in this one file. Python is used
# rather than awk because the awk shipped with macOS reads about a tenth as many records per
# second, and awk implementations disagree on writing NUL bytes.
cat > "$tmp/scan.py" <<'PYTHON'
"""Reads `samtools view` text on stdin and writes the per-series corpora, their slice
sidecars and a scan summary into an output directory.

Usage: scan.py OUT_DIR DONE_MARKER RECORD_CAP NAME:TARGET_BYTES... region=CHROM:START-...

NAME is MC, MD, XA, SA or QUAL. The region arguments name the regions the input was drawn
from, in order, and only label the per-region record counts in the summary. Every input
line is one record, and slices count records across the whole input (they do not reset
between regions). A series stops
accumulating at the first 100,000-record boundary where it holds at least TARGET_BYTES.
When every series has stopped, or RECORD_CAP records have been read, DONE_MARKER is
created so the caller stops feeding input, and the remaining input is drained unread.
"""
import sys

SMALL_SLICE_RECORDS = 10_000
LARGE_SLICE_RECORDS = 100_000
TAG_NAMES = ("MC", "MD", "XA", "SA")


class Series:
    """One output stream: its data file, slice sidecars and the current slice's values."""

    def __init__(self, name, target_bytes, out_dir):
        self.name = name
        self.target_bytes = target_bytes
        self.is_tag = name in TAG_NAMES
        self.terminator = b"\0" if self.is_tag else b"\n"
        # The leading tab anchors the match to the start of a tag, never inside a value.
        self.marker = b"\t" + name.encode() + b":Z:" if self.is_tag else None
        self.data_file = open(f"{out_dir}/{name}.bin", "wb")
        self.small_sidecar = open(f"{out_dir}/{name}.slices-10k.txt", "w")
        self.large_sidecar = open(f"{out_dir}/{name}.slices-100k.txt", "w")
        self.pending = []
        self.bytes_written = 0
        self.values_written = 0
        self.last_large_offset = 0
        self.full = False
        self.records_covered = None

    def end_small_slice(self):
        """Writes the values gathered since the last 10k boundary; a slice with none adds nothing."""
        if not self.pending:
            return
        data = self.terminator.join(self.pending) + self.terminator
        self.values_written += len(self.pending)
        self.pending.clear()
        self.data_file.write(data)
        self.bytes_written += len(data)
        self.small_sidecar.write(f"{self.bytes_written}\n")

    def end_large_slice(self, records):
        """Records the 100k boundary offset; past its target the series is full, having covered `records`."""
        if self.bytes_written > self.last_large_offset:
            self.large_sidecar.write(f"{self.bytes_written}\n")
            self.last_large_offset = self.bytes_written
        if self.bytes_written >= self.target_bytes:
            self.full = True
            self.records_covered = records

    def close(self):
        for handle in (self.data_file, self.small_sidecar, self.large_sidecar):
            handle.close()


def main():
    out_dir, done_marker, record_cap = sys.argv[1], sys.argv[2], int(sys.argv[3])
    series = []
    regions = []
    for spec in sys.argv[4:]:
        if spec.startswith("region="):
            regions.append(spec[len("region="):])
        else:
            name, target = spec.split(":")
            series.append(Series(name, int(target), out_dir))
    if len(series) != 5:
        sys.exit("scan.py: expected the MC, MD, XA, SA and QUAL series")

    qual_series = next(s for s in series if not s.is_tag)
    tag_series = [s for s in series if s.is_tag]
    active_tag_series = list(tag_series)
    chromosome_records = {}
    records = 0
    last_position = b""
    stop_reason = "end_of_input"

    for line in sys.stdin.buffer:
        # Only QUAL (field 11) and the tags (field 12 onwards) are needed.
        fields = line.rstrip(b"\n").split(b"\t", 11)
        records += 1
        chromosome = fields[2]
        last_position = fields[3]
        chromosome_records[chromosome] = chromosome_records.get(chromosome, 0) + 1

        if not qual_series.full:
            qual_series.pending.append(fields[10])
        if active_tag_series and len(fields) == 12:
            tags = b"\t" + fields[11]
            for s in active_tag_series:
                start = tags.find(s.marker)
                if start >= 0:
                    start += 6
                    end = tags.find(b"\t", start)
                    s.pending.append(tags[start:end] if end >= 0 else tags[start:])

        if records % SMALL_SLICE_RECORDS == 0:
            for s in series:
                if not s.full:
                    s.end_small_slice()
            if records % LARGE_SLICE_RECORDS == 0:
                for s in series:
                    if not s.full:
                        s.end_large_slice(records)
                active_tag_series = [s for s in tag_series if not s.full]
                if all(s.full for s in series):
                    stop_reason = "all_series_full"
                    break
                if records >= record_cap:
                    stop_reason = "record_cap"
                    break

    if stop_reason == "end_of_input":
        for s in series:
            if not s.full:
                s.end_small_slice()
                s.end_large_slice(records)
    else:
        open(done_marker, "w").close()
        while sys.stdin.buffer.read(1 << 20):
            pass

    for s in series:
        if s.records_covered is None:
            s.records_covered = records

    with open(f"{out_dir}/scan-summary.tsv", "w") as summary:
        summary.write(f"records_scanned\tall\t{records}\n")
        summary.write(f"stop_reason\tall\t{stop_reason}\n")
        for s in series:
            reached = "yes" if s.bytes_written >= s.target_bytes else "no"
            summary.write(f"values\t{s.name}\t{s.values_written}\n")
            summary.write(f"bytes\t{s.name}\t{s.bytes_written}\n")
            summary.write(f"target_bytes\t{s.name}\t{s.target_bytes}\n")
            summary.write(f"target_reached\t{s.name}\t{reached}\n")
            summary.write(f"records_covered\t{s.name}\t{s.records_covered}\n")
        for region in regions:
            count = chromosome_records.get(region.split(":")[0].encode(), 0)
            if count:
                summary.write(f"region_records\t{region}\t{count}\n")
        if records:
            summary.write(f"last_record_position\t{chromosome.decode()}\t{last_position.decode()}\n")
    for s in series:
        s.close()


main()
PYTHON

# Prints one value from a scan-summary.tsv: summary_value FILE KIND KEY.
summary_value() {
    awk -F '\t' -v kind="$2" -v key="$3" '$1 == kind && $2 == key { print $3 }' "$1"
}

# Streams the sample's regions through the scan helper, in order, until it reports done.
# The helper drains whatever is left of the current region, so samtools is never killed
# by SIGPIPE and any non-zero samtools exit is a genuine failure.
scan_sample() {
    sample=$1
    regions=$2
    work=$3
    done_marker=$tmp/$sample.done
    failed_marker=$tmp/$sample.samtools-failed
    region_args=$(for region in $regions; do printf 'region=%s ' "$region"; done)
    {
        for region in $regions; do
            if [ -e "$done_marker" ]; then
                break
            fi
            samtools view -@ "$SAMTOOLS_THREADS" -T "$REFERENCE" --keep-tag MC,MD,XA,SA \
                "$CRAM_DIR/$sample.final.cram" "$region" || {
                : > "$failed_marker"
                break
            }
        done
    } | python3 "$tmp/scan.py" "$work" "$done_marker" "$RECORD_CAP" \
        MC:"$MC_TARGET_BYTES" MD:"$MD_TARGET_BYTES" XA:"$XA_TARGET_BYTES" \
        SA:"$SA_TARGET_BYTES" QUAL:"$QUAL_TARGET_BYTES" $region_args
    if [ -e "$failed_marker" ]; then
        die "samtools failed while scanning $sample"
    fi
}

# Checks one sidecar: whole numbers, strictly increasing, last offset equal to the file size.
check_sidecar() {
    sidecar=$1
    size=$2
    awk -v size="$size" '
        $0 !~ /^[0-9]+$/ || $1 + 0 <= previous { bad = 1 }
        { previous = $1 + 0 }
        END { if (bad || NR == 0 || previous != size) exit 1 }
    ' "$sidecar" || die "bad slice sidecar: $sidecar (size $size)"
}

# Fails loudly unless every output of one sample is internally consistent.
validate_sample() {
    sample=$1
    dir=$2
    summary=$dir/scan-summary.tsv
    for series in $SERIES_NAMES; do
        bin=$dir/$series.bin
        [ -s "$bin" ] || die "$sample $series.bin is empty"
        size=$(wc -c < "$bin" | tr -d ' ')
        expected=$(summary_value "$summary" values "$series")
        if [ "$series" = QUAL ]; then
            terminator='\n'
        else
            terminator='\000'
        fi
        terminators=$(tr -cd "$terminator" < "$bin" | wc -c | tr -d ' ')
        [ "$terminators" -eq "$expected" ] ||
            die "$sample $series.bin has $terminators terminators but $expected values were written"
        [ "$(tail -c 1 "$bin" | od -An -tx1 | tr -d ' ')" = "$(printf "$terminator" | od -An -tx1 | tr -d ' ')" ] ||
            die "$sample $series.bin does not end with its terminator"
        [ "$(summary_value "$summary" bytes "$series")" -eq "$size" ] ||
            die "$sample $series.bin size differs from the scan summary"

        check_sidecar "$dir/$series.slices-10k.txt" "$size"
        check_sidecar "$dir/$series.slices-100k.txt" "$size"
        awk 'NR == FNR { small[$1] = 1; next } !($1 in small) { exit 1 }' \
            "$dir/$series.slices-10k.txt" "$dir/$series.slices-100k.txt" ||
            die "$sample $series: a 100k slice boundary is missing from the 10k sidecar"
    done
}

# Prints the scan statistics of one sample and warns about any series short of its target.
report_sample() {
    sample=$1
    role=$2
    summary=$3
    seconds=$4
    records=$(summary_value "$summary" records_scanned all)
    echo "$sample ($role): scanned $records records, stop reason $(summary_value "$summary" stop_reason all), ${seconds}s elapsed"
    awk -F '\t' '$1 == "region_records" { printf "  %s: %s records\n", $2, $3 }' "$summary"
    for series in $SERIES_NAMES; do
        printf '  %-4s %9s values %10s bytes (target %s)\n' "$series" \
            "$(summary_value "$summary" values "$series")" \
            "$(summary_value "$summary" bytes "$series")" \
            "$(summary_value "$summary" target_bytes "$series")"
        if [ "$(summary_value "$summary" target_reached "$series")" != yes ]; then
            echo "  WARNING: $sample $series did not reach its target within $RECORD_CAP records" >&2
        fi
    done
}

extract_sample() {
    sample=$1
    role=$2
    chromosomes=$3
    regions=$(for chromosome in $chromosomes; do printf '%s:%s- ' "$chromosome" "$SCAN_START"; done)
    work=$tmp/$sample
    mkdir "$work"
    echo "Extracting $sample ($role) from $regions..."
    started=$(date +%s)
    scan_sample "$sample" "$regions" "$work"
    seconds=$(($(date +%s) - started))
    validate_sample "$sample" "$work"
    report_sample "$sample" "$role" "$work/scan-summary.tsv" "$seconds"
    mkdir -p "$OUT_DIR/$sample"
    mv "$work"/* "$OUT_DIR/$sample/"
}

mkdir -p "$OUT_DIR"
extract_sample "$TRAINING_SAMPLE" training "$TRAINING_CHROMOSOMES"
extract_sample "$HELDOUT_SAMPLE" held-out "$HELDOUT_CHROMOSOMES"

# Checksums use paths relative to OUT_DIR so `sha256sum -c SHA256SUMS` works from there.
(
    cd "$OUT_DIR"
    set --
    for sample in $TRAINING_SAMPLE $HELDOUT_SAMPLE; do
        for file in "$sample"/*; do
            set -- "$@" "$file"
        done
    done
    sha256_of_files "$@" > "$tmp/SHA256SUMS"
)
mv "$tmp/SHA256SUMS" "$OUT_DIR/SHA256SUMS"
echo "Wrote $OUT_DIR/SHA256SUMS"
