#!/bin/bash
# Runs a fuzzing campaign on one of the targets in fuzz/ and keeps what it
# finds.  For Linux machines with clang and libFuzzer (Ubuntu 24.04: clang-18
# and libclang-rt-18-dev); it also runs on macOS with a clang that has a
# working libFuzzer and AddressSanitizer.
#
# usage: fuzz/campaign.sh [-o WORKDIR] [-j JOBS] [-s SLICE] [-c CC]
#                         [-l LOCKFILE] TARGET SECONDS
#
#   TARGET    decompress, compress, sort, or sort_form_<form> (apple_clang,
#             clang, gcc, apple_gcc)
#   SECONDS   wall-clock time to fuzz, shared among the kernel tiers
#   -o        work directory (default ~/bz2ng-fuzz); kept between runs:
#               build/                  the instrumented build
#               seeds/TARGET/           the generated seed corpus
#               corpus/TARGET/          the corpus, grown by every run
#               artifacts/TARGET/       crash-*, timeout-*, oom-* inputs
#                                       (libFuzzer's artefacts)
#               logs/                   one libFuzzer log per slice
#             The seeds are rewritten by every run; a file the generator no
#             longer writes stays and is used as one more input.
#   -j        parallel fuzzing processes (default: the number of CPUs)
#   -s        seconds per slice (default 3600): the run rotates over every
#             kernel tier this CPU has, one slice at a time, all slices on
#             the one corpus
#   -c        C compiler (default clang-18 if installed, otherwise clang)
#   -l        lock file held for the whole run, waiting for it if need be
#             (default /tmp/bz2ng.lock; "none" for no lock; flock(1) is
#             needed otherwise)
#
# A summary is printed at the end: executions and coverage per slice (the
# last status line of each), the corpus, and the artefacts.  Exit status 0
# when the run added no artefact, 1 when it did, 2 on a usage or build error.
# To reproduce an artefact: BZ2NG_TIER=<tier> WORKDIR/build/fuzz/fuzz_TARGET
# <artefact>.
set -euo pipefail

usage() {
    sed -n '7,33p' "$0" | sed 's/^# \{0,1\}//' >&2
    exit 2
}

# The arguments as given, for the re-run under the lock below.
args=("$@")
workdir="$HOME/bz2ng-fuzz"
jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
slice=3600
cc=""
lockfile=/tmp/bz2ng.lock
while getopts "o:j:s:c:l:h" opt; do
    case $opt in
    o) workdir=$OPTARG ;;
    j) jobs=$OPTARG ;;
    s) slice=$OPTARG ;;
    c) cc=$OPTARG ;;
    l) lockfile=$OPTARG ;;
    *) usage ;;
    esac
done
shift $((OPTIND - 1))
[ $# -eq 2 ] || usage
target=$1
seconds=$2
case $target in
decompress | compress | sort | sort_form_apple_clang | sort_form_clang | \
    sort_form_gcc | sort_form_apple_gcc) ;;
*)
    echo "campaign.sh: unknown target $target" >&2
    exit 2
    ;;
esac

# Hold the lock for the whole run: re-run this script under flock.
if [ "$lockfile" != none ] && [ -z "${BZ2NG_FUZZ_LOCKED:-}" ]; then
    if ! command -v flock >/dev/null; then
        echo "campaign.sh: flock not found; give -l none to run unlocked" >&2
        exit 2
    fi
    echo "campaign.sh: waiting for $lockfile"
    BZ2NG_FUZZ_LOCKED=1 exec flock "$lockfile" "$0" "${args[@]}"
fi

src=$(cd "$(dirname "$0")/.." && pwd)
if [ -z "$cc" ]; then
    cc=$(command -v clang-18 || command -v clang || true)
fi
mkdir -p "$workdir"/logs "$workdir/corpus/$target" "$workdir/artifacts/$target"
workdir=$(cd "$workdir" && pwd)
build=$workdir/build
corpus=$workdir/corpus/$target
artifacts=$workdir/artifacts/$target
seeds=$workdir/seeds
# The seeds of the sorter's form variants are the sorter's.
seed_dir=$seeds/${target%%_form_*}
binary=$build/fuzz/fuzz_$target

echo "campaign.sh: building fuzz_$target with $cc in $build"
cmake -S "$src" -B "$build" -DCMAKE_C_COMPILER="$cc" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBZ2NG_BUILD_FUZZ=ON \
    -DBZ2NG_BUILD_PROGRAMS=OFF -DBZ2NG_BUILD_BENCH=OFF >"$workdir/logs/cmake.log"
cmake --build "$build" -j "$jobs" --target "fuzz_$target" fuzz_seeds \
    >>"$workdir/logs/cmake.log" 2>&1 || {
    tail -30 "$workdir/logs/cmake.log" >&2
    echo "campaign.sh: build failed (log: $workdir/logs/cmake.log)" >&2
    exit 2
}
"$build/fuzz/fuzz_seeds" "$seeds"

# Per-target limits: the longest input, the time one input may take, and
# the memory one process may use.  The decoder's inputs are compressed
# streams (the seeds go up to about 33 kB); the encoder's and the sorter's
# grow by repetition inside the target, so short inputs reach large blocks.
case $target in
decompress) max_len=131072 ;;
compress) max_len=8192 ;;
*) max_len=32768 ;;
esac
export ASAN_OPTIONS=${ASAN_OPTIONS:-quarantine_size_mb=64:malloc_context_size=10}
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1}
# libFuzzer's fork-mode parent leaks a few bytes of its own at exit (clang
# 18), which LeakSanitizer would report as a crash with an empty input; the
# workers' leak checks of each input are unaffected.
echo "leak:fuzzer::FuzzWithFork" >"$workdir/lsan.supp"
export LSAN_OPTIONS=${LSAN_OPTIONS:-suppressions=$workdir/lsan.supp}

# The kernel tiers this CPU has: each target checks $BZ2NG_TIER and says
# "skipped" for a tier the CPU lacks (test/test_tier.h).
case $(uname -m) in
x86_64 | amd64) candidates="avx2 sse4 scalar" ;;
aarch64 | arm64) candidates="neon scalar" ;;
*) candidates="scalar" ;;
esac
tiers=""
probe_dir=$workdir/logs/probe
mkdir -p "$probe_dir"
for t in $candidates; do
    out=$(BZ2NG_TIER=$t "$binary" -runs=0 "$probe_dir" 2>&1 || true)
    case $out in
    *"not available on this CPU"*) echo "campaign.sh: tier $t not available here" ;;
    *) tiers="$tiers $t" ;;
    esac
done
tiers=${tiers# }
echo "campaign.sh: fuzz_$target, tiers: $tiers; $seconds s with $jobs processes, slices of $slice s"

start=$(date +%s)
summary=$workdir/logs/summary-$target-$start.txt
artefacts_before=$(ls "$artifacts" | wc -l)
n=0
: >"$summary"
while :; do
    for t in $tiers; do
        left=$((start + seconds - $(date +%s)))
        [ "$left" -gt 0 ] || break 2
        this=$((left < slice ? left : slice))
        n=$((n + 1))
        log=$workdir/logs/$target-$start-$n-$t.log
        before=$(ls "$artifacts" | wc -l)
        echo "campaign.sh: slice $n, tier $t, $this s (log $log)"
        # Fork mode keeps going after a crash, timeout or OOM and writes
        # each such input to the artefact directory.
        BZ2NG_TIER=$t "$binary" -fork="$jobs" -ignore_crashes=1 \
            -ignore_timeouts=1 -ignore_ooms=1 -max_total_time="$this" \
            -max_len="$max_len" -timeout=60 -rss_limit_mb=2560 \
            -print_final_stats=1 -artifact_prefix="$artifacts/" \
            "$corpus" "$seed_dir" >"$log" 2>&1 || true
        # In fork mode -print_final_stats reports the parent's own (zero)
        # executions; the last status line has the slice's total.
        last=$(grep -E '^#[0-9]+' "$log" | tail -1 | tr '\t' ' ' || true)
        found=$(($(ls "$artifacts" | wc -l) - before))
        echo "slice $n tier $t ${this}s: ${last:-no status line} new artefacts: $found" |
            tee -a "$summary"
    done
done

files=$(ls "$corpus" | wc -l)
bytes=$(du -sk "$corpus" | cut -f1)
execs=$(sed -n 's/^slice .*: #\([0-9]*\).*/\1/p' "$summary" |
    awk '{s += $1} END {print s + 0}')
new_artefacts=$(($(ls "$artifacts" | wc -l) - artefacts_before))
{
    echo "== fuzz_$target: $(($(date +%s) - start)) s on tiers $tiers, $jobs processes"
    echo "executions: $execs"
    echo "corpus: $files inputs, $bytes kB in $corpus"
    echo "artefacts: $new_artefacts new in this run; all of them, in $artifacts:"
    ls -lt "$artifacts" | sed -n '2,$p'
} | tee -a "$summary"
echo "summary kept in $summary"
[ "$new_artefacts" -eq 0 ]
