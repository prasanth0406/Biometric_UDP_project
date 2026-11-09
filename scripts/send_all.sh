#!/usr/bin/env bash
# scripts/send_all.sh - Sequentially transfer real biometric files and verify SHA-256 hashes side-by-side
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${ROOT_DIR}"

REAL_DIR="sample_data/real_biometric"
RECV_DIR="received_files"
LIMIT="${1:-}"

mkdir -p "${RECV_DIR}" logs results

if [ ! -d "${REAL_DIR}" ]; then
    echo "[ERROR] Real biometric directory '${REAL_DIR}' not found."
    exit 1
fi

# If python3 or python is available, prefer Python dataset transmission engine
PYTHON_BIN=""
if command -v python3 >/dev/null 2>&1; then
    PYTHON_BIN="python3"
elif command -v python >/dev/null 2>&1; then
    PYTHON_BIN="python"
fi

if [ -n "${PYTHON_BIN}" ]; then
    PY_ARGS=()
    if [ -n "${LIMIT}" ] && [ "${LIMIT}" -gt 0 ]; then
        PY_ARGS+=("--limit" "${LIMIT}")
    fi
    exec "${PYTHON_BIN}" scripts/transfer_dataset.py "${PY_ARGS[@]}"
fi

# Fallback pure-bash runner
PORT=6200
INDEX=0
find "${REAL_DIR}" -maxdepth 1 -type f | sort | while IFS= read -r file; do
    INDEX=$((INDEX + 1))
    if [ -n "${LIMIT}" ] && [ "${LIMIT}" -gt 0 ] && [ "${INDEX}" -gt "${LIMIT}" ]; then
        break
    fi
    BASE_NAME=$(basename "${file}")
    RECV_FILE="${RECV_DIR}/${BASE_NAME}"

    rm -f "${RECV_FILE}"
    PORT=$((PORT + 1))

    ./bin/receiver -p "${PORT}" -o "${RECV_DIR}" -q -1 &
    RECV_PID=$!
    sleep 0.05

    ./bin/sender -f "${file}" -p "${PORT}" -c 8192 -w 10 -q 2>&1 || true
    wait "${RECV_PID}" 2>/dev/null || true

    ORIG_HASH=$(sha256sum "${file}" | awk '{print $1}')
    if [ -f "${RECV_FILE}" ]; then
        RECV_HASH=$(sha256sum "${RECV_FILE}" | awk '{print $1}')
    else
        RECV_HASH="FILE_NOT_FOUND"
    fi

    if [ "${ORIG_HASH}" = "${RECV_HASH}" ]; then
        STATUS="PASS"
    else
        STATUS="FAIL"
    fi

    printf "%4d | %-24s | %-64s | %-64s | %-6s\n" "${INDEX}" "${BASE_NAME}" "${ORIG_HASH}" "${RECV_HASH}" "${STATUS}"
done
