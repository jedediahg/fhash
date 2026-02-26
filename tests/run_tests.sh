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
cp "${WORK}/source/Hard Link Hearts.mp3" "${WORK}/dupes/"
cp "${WORK}/source/Hard Link Hearts (1).mp3" "${WORK}/dupes/"
cp "${WORK}/source/One Beating Core.mp3" "${WORK}/dupes/"
cp "${WORK}/source/One Beating Core (1).mp3" "${WORK}/dupes/"
cp "${WORK}/source/0bytes.mp3" "${WORK}/dupes/"
cp "${WORK}/source/BadAudio.mp3" "${WORK}/dupes/"

run_cmd() {
    local label="$1"
    shift
    echo "== ${label} ==" >> "${OUT}"
    "$@" >> "${OUT}" 2>&1 || true
    echo >> "${OUT}"
}

echo "[INFO] Running scans and duplicate checks..."
run_cmd "scan (file+audio)" ./fhash scan -r -h -a -f -s "${WORK}" -e mp3 -d "${DB}"
run_cmd "dupe file hash" ./fhash dupe -xh2 -s "${WORK}" -r -e mp3 -d "${DB}"
run_cmd "dupe audio hash" ./fhash dupe -xa2 -s "${WORK}" -r -e mp3 -d "${DB}"
run_cmd "link dry-run (file hash, shallowest)" ./fhash link -xh2 -ls -s "${WORK}/dupes" -r -e mp3 -d "${DB}" -dry
run_cmd "db contents" sqlite3 "${DB}" "SELECT filepath, md5, audio_md5 FROM files ORDER BY filepath;"

echo "[INFO] Results written to ${OUT}"
