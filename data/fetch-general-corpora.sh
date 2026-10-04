#!/bin/sh
# Fetches the general-purpose benchmark corpora: Silesia into silesia/, everything else into
# general/ (both next to this script and git-ignored; nothing fetched here is committed).
#
# Every item is pinned by the SHA-256 of the final file. An item already present with the
# right hash is left untouched; anything else is downloaded, extracted to a ".partial" file,
# verified and only then moved into place, so an interrupted or corrupted download never
# leaves a plausible-looking file behind. See data/README.md for what each item is.
#
# Usage: fetch-general-corpora.sh
#
# Requires curl, unzip, tar and gzip, plus sha256sum or shasum.

set -eu
LC_ALL=C
export LC_ALL

data_dir=$(cd "$(dirname "$0")" && pwd)

die() {
    echo "fetch-general-corpora.sh: error: $*" >&2
    exit 1
}

for tool in curl unzip tar gzip; do
    command -v "$tool" >/dev/null 2>&1 || die "$tool not found on PATH"
done
if command -v sha256sum >/dev/null 2>&1; then
    sha256_of() { sha256sum "$1" | cut -d ' ' -f 1; }
elif command -v shasum >/dev/null 2>&1; then
    sha256_of() { shasum -a 256 "$1" | cut -d ' ' -f 1; }
else
    die "neither sha256sum nor shasum found on PATH"
fi

# One item per line: destination (relative to this directory), SHA-256 of the final file,
# how to obtain it from the download, download URL, member inside the download ("-" if none)
# and a fallback URL tried only if the first fails to download ("-" if none).
#
#   raw     the download is the file
#   gunzip  the download is the file, gzip-compressed
#   zip     the file is a member of a zip archive
#   tgz     the file is a member of a gzip-compressed tar archive
#
# Choices that are not obvious from the table:
# - Silesia: both URLs serve zips with identical contents, though the zips themselves differ.
# - Canterbury Large: corpus.canterbury.ac.nz answers scripted requests with 403, so the
#   data-compression.info mirror is primary; the three files carry the original 1997 content.
# - Python source: a tarball of a pinned release; the .tar is what gunzip of the release
#   .tgz yields, so the pin is reproducible.
# - Android.log: from the Loghub v1 record on Zenodo (a fixed record id, not a moving "latest").
# - citylots.json: the URL names the single commit that ever touched the file, not "master".
items() {
    cat <<'ITEMS'
silesia/dickens b24c37886142e11d0ee687db6ab06f936207aa7f2ea1fd1d9a36763c7a507e6a zip https://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip dickens https://mattmahoney.net/dc/silesia.zip
silesia/mozilla 657fc3764b0c75ac9de9623125705831ebbfbe08fed248df73bc2dc66e2a963b zip https://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip mozilla https://mattmahoney.net/dc/silesia.zip
silesia/mr 68637ed52e3e4860174ed2dc0840ac77d5f1a60abbcb13770d5754e3774d53e6 zip https://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip mr https://mattmahoney.net/dc/silesia.zip
silesia/nci fc63a31770947b8c2062d3b19ca94c00485a232bb91b502021948fee983e1635 zip https://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip nci https://mattmahoney.net/dc/silesia.zip
silesia/ooffice e7ee013880d34dd5208283d0d3d91b07f442e067454276095ded14f322a656eb zip https://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip ooffice https://mattmahoney.net/dc/silesia.zip
silesia/osdb 60f027179302ca3ad87c58ac90b6be72ec23588aaa7a3b7fe8ecc0f11def3fa3 zip https://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip osdb https://mattmahoney.net/dc/silesia.zip
silesia/reymont 0eac0114a3dfe6e2ee1f345a0f79d653cb26c3bc9f0ed79238af4933422b7578 zip https://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip reymont https://mattmahoney.net/dc/silesia.zip
silesia/samba 93ba07bc44d8267789c1d911992f40b089ffa2140b4a160fac11ccae9a40e7b2 zip https://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip samba https://mattmahoney.net/dc/silesia.zip
silesia/sao c2d0ea2cc59d4c21b7fe43a71499342a00cbe530a1d5548770e91ecd6214adcc zip https://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip sao https://mattmahoney.net/dc/silesia.zip
silesia/webster 6a68f69b26daf09f9dd84f7470368553194a0b294fcfa80f1604efb11143a383 zip https://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip webster https://mattmahoney.net/dc/silesia.zip
silesia/x-ray 7de9fce1405dc44ae5e6813ed21cd5751e761bd4265655a005d39b9685d1c9ad zip https://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip x-ray https://mattmahoney.net/dc/silesia.zip
silesia/xml 0e82e54e695c1938e4193448022543845b33020c8be6bf3bf3ead2224903e08c zip https://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip xml https://mattmahoney.net/dc/silesia.zip
general/E.coli 9125dfd87315961ef4286f3856098069e050cc3a2abe65735fe43e69d1996f40 zip https://www.data-compression.info/files/corpora/largecanterburycorpus.zip E.coli https://corpus.canterbury.ac.nz/resources/large.zip
general/bible.txt 4e0a7e8dff7d9c82dbded57305c0ca3cdd3c4ca014db27121782fe9710f4723f zip https://www.data-compression.info/files/corpora/largecanterburycorpus.zip bible.txt https://corpus.canterbury.ac.nz/resources/large.zip
general/world192.txt 1aebdc97d29904b25791da9aa32be90b69d7da6dc0ac9b95512ed27ed40d2112 zip https://www.data-compression.info/files/corpora/largecanterburycorpus.zip world192.txt https://corpus.canterbury.ac.nz/resources/large.zip
general/enwik8 2b49720ec4d78c3c9fabaee6e4179a5e997302b3a70029f30f2d582218c024a8 zip https://mattmahoney.net/dc/enwik8.zip enwik8 -
general/Python-3.12.7.tar 9778a3a9df2e85f48e434f08fc2baa9c59aaeded15c3ef500e76538f20b9071c gunzip https://www.python.org/ftp/python/3.12.7/Python-3.12.7.tgz - -
general/Android.log 0f35aa11b67ec07cd34950e5c518961cb841ec0925ce007f16f3eeb0a7d35a07 tgz https://zenodo.org/api/records/3227177/files/Android.tar.gz/content Android.log -
general/citylots.json cd0d4a4a2e7629618902a54c92514f7a14761994dd7800e489ab98b96628f2bc raw https://raw.githubusercontent.com/zemirco/sf-city-lots-json/33c27c137784a96d0fbd7f329dceda6cc7f49fa3/citylots.json - -
ITEMS
}

tmp=$(mktemp -d "${TMPDIR:-/tmp}/fetch-general-corpora.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
trap 'exit 1' HUP INT TERM

# Downloads URL once per run into the temp directory and prints the local path.
download() {
    url=$1
    archive=$tmp/$(printf '%s' "$url" | cksum | cut -d ' ' -f 1)
    if [ ! -f "$archive" ]; then
        echo "  downloading $url" >&2
        curl -fsSL --retry 3 --retry-delay 5 --connect-timeout 30 -o "$archive.partial" "$url" || {
            rm -f "$archive.partial"
            return 1
        }
        mv "$archive.partial" "$archive"
    fi
    echo "$archive"
}

# Writes the item's bytes to stdout: extract KIND ARCHIVE MEMBER.
extract() {
    case $1 in
        raw) cat "$2" ;;
        gunzip) gzip -dc "$2" ;;
        zip) unzip -p "$2" "$3" ;;
        tgz) tar -xzOf "$2" "$3" ;;
        *) die "unknown extraction kind: $1" ;;
    esac
}

items | while read -r destination expected_sha256 kind url member fallback_url; do
    path=$data_dir/$destination
    if [ -f "$path" ] && [ "$(sha256_of "$path")" = "$expected_sha256" ]; then
        echo "ok       $destination (already present)"
        continue
    fi
    echo "fetching $destination"
    mkdir -p "$(dirname "$path")"
    if archive=$(download "$url"); then
        source_url=$url
    elif [ "$fallback_url" != "-" ] && archive=$(download "$fallback_url"); then
        source_url=$fallback_url
    else
        die "could not download $url"
    fi
    extract "$kind" "$archive" "$member" > "$path.partial" ||
        die "could not extract $destination from $source_url"
    actual_sha256=$(sha256_of "$path.partial")
    if [ "$actual_sha256" != "$expected_sha256" ]; then
        rm -f "$path.partial"
        die "$destination from $source_url has SHA-256 $actual_sha256, expected $expected_sha256"
    fi
    mv "$path.partial" "$path"
    echo "ok       $destination ($(wc -c < "$path" | tr -d ' ') bytes, from $source_url)"
done
echo "All items present and verified."
