#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${ROOT}/test_results.txt"
WORK="${ROOT}/tests/workdir"
SRC="${ROOT}/test_source"
DB="${WORK}/test.db"

echo "[INFO] Preparing test workspace..."
rm -f "${OUT}"
rm -rf "${WORK}"
mkdir -p "${WORK}/dupes"

echo "[INFO] Copying source samples..."
cp -R "${SRC}" "${WORK}/source"

echo "[INFO] Seeding dupes folder..."
find "${WORK}/source" -maxdepth 1 -type f -name "*.mp3" | head -n 6 | while read -r f; do
  cp "$f" "${WORK}/dupes/"
done

run_cmd() {
    local label="$1"
    shift
    echo "== ${label} ==" >> "${OUT}"
    "$@" >> "${OUT}" 2>&1 || true
    echo >> "${OUT}"
}

echo "[INFO] Running scans and duplicate checks..."
run_cmd "scan (file+audio)" ./fhash scan -v -r -h -a -f -s "${WORK}" -e mp3 -d "${DB}"
run_cmd "post-scan db" sqlite3 "${DB}" "SELECT filepath, md5, audio_md5 FROM files ORDER BY filepath;"
run_cmd "post-scan ll" ls -lR "${WORK}"

run_cmd "dupe file hash" ./fhash dupe -v -xh2 -s "${WORK}" -r -e mp3 -d "${DB}"
run_cmd "post-dupe db" sqlite3 "${DB}" "SELECT filepath, md5, audio_md5 FROM files ORDER BY filepath;"

run_cmd "dupe audio hash" ./fhash dupe -v -xa2 -s "${WORK}" -r -e mp3 -d "${DB}"
run_cmd "post-dupe-audio db" sqlite3 "${DB}" "SELECT filepath, md5, audio_md5 FROM files ORDER BY filepath;"

run_cmd "link dry-run (file hash, shallowest)" ./fhash link -v -xh2 -ls -s "${WORK}/dupes" -r -e mp3 -d "${DB}" -dry
run_cmd "post-link dry-run ll" ls -lR "${WORK}"

echo "[INFO] Results written to ${OUT}"
