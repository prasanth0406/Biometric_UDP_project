#!/usr/bin/env bash
# scripts/test_e2e.sh - End-to-End Localhost Integration Tests
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${ROOT_DIR}"

PORT=5400
SAMPLE_FILE="${1:-}"
if [ -z "${SAMPLE_FILE}" ]; then
    for f in sample_data/real_biometric/*.*; do
        if [ -f "$f" ]; then
            SAMPLE_FILE="$f"
            break
        fi
    done
fi
if [ -z "${SAMPLE_FILE}" ] || [ ! -f "${SAMPLE_FILE}" ]; then
    echo "[ERROR] No real biometric file found in sample_data/real_biometric/"
    exit 1
fi

RECV_DIR="received_files"
BASE_NAME="$(basename "${SAMPLE_FILE}")"
RECV_FILE="${RECV_DIR}/${BASE_NAME}"
FILE_SIZE=$(wc -c < "${SAMPLE_FILE}" | tr -d ' ')

mkdir -p "${RECV_DIR}" logs results

echo "========================================================"
echo " Starting End-to-End Localhost Verification"
echo "========================================================"
echo "[INFO] Biometric File: ${BASE_NAME} (${FILE_SIZE} bytes)"

ORIGINAL_HASH=$(sha256sum "${SAMPLE_FILE}" | awk '{print $1}')
echo "[TEST] Original Biometric File Hash: ${ORIGINAL_HASH}"

# Test 1: Stop-and-Wait (N=1)
echo ""
echo "--> Test 1: Stop-and-Wait Mode (Chunk=1024, Window=1)"
PORT=$((PORT + 1))
rm -f "${RECV_FILE}"
./bin/receiver -p "${PORT}" -o "${RECV_DIR}" -q -1 &
RECV_PID=$!
sleep 0.1

./bin/sender -f "${SAMPLE_FILE}" -p "${PORT}" -c 1024 -w 1 -q
wait "${RECV_PID}"

RECV_HASH=$(sha256sum "${RECV_FILE}" | awk '{print $1}')
if cmp -s "${SAMPLE_FILE}" "${RECV_FILE}"; then
    echo "  [PASS] Stop-and-Wait: Reconstructed file bit-for-bit identical!"
    echo "         Original SHA-256: ${ORIGINAL_HASH}"
    echo "         Received SHA-256: ${RECV_HASH}"
else
    echo "  [FAIL] Stop-and-Wait: File mismatch!"
    exit 1
fi

# Test 2: Sliding-Window (N=4)
echo ""
echo "--> Test 2: Sliding Window Mode (Chunk=2048, Window=4)"
PORT=$((PORT + 1))
rm -f "${RECV_FILE}"
./bin/receiver -p "${PORT}" -o "${RECV_DIR}" -q -1 &
RECV_PID=$!
sleep 0.1

./bin/sender -f "${SAMPLE_FILE}" -p "${PORT}" -c 2048 -w 4 -q
wait "${RECV_PID}"

RECV_HASH=$(sha256sum "${RECV_FILE}" | awk '{print $1}')
if cmp -s "${SAMPLE_FILE}" "${RECV_FILE}"; then
    echo "  [PASS] Sliding Window (N=4): Reconstructed file bit-for-bit identical!"
    echo "         Original SHA-256: ${ORIGINAL_HASH}"
    echo "         Received SHA-256: ${RECV_HASH}"
else
    echo "  [FAIL] Sliding Window (N=4): File mismatch!"
    exit 1
fi

# Test 3: Fault Injection (Corrupted Chunk Retransmission)
echo ""
echo "--> Test 3: Fault Injection (Corrupt probability 15% with Stop-and-Wait)"
PORT=$((PORT + 1))
rm -f "${RECV_FILE}"
./bin/receiver -p "${PORT}" -o "${RECV_DIR}" -q -1 &
RECV_PID=$!
sleep 0.1

./bin/sender -f "${SAMPLE_FILE}" -p "${PORT}" -c 1024 -w 1 -k 0.15 -q
wait "${RECV_PID}"

RECV_HASH=$(sha256sum "${RECV_FILE}" | awk '{print $1}')
if cmp -s "${SAMPLE_FILE}" "${RECV_FILE}"; then
    echo "  [PASS] Fault Injection: CRC mismatch detected, NACK/Retransmit recovered file!"
    echo "         Original SHA-256: ${ORIGINAL_HASH}"
    echo "         Received SHA-256: ${RECV_HASH}"
else
    echo "  [FAIL] Fault Injection: File recovery failed!"
    exit 1
fi

# Test 4: Impairment Proxy (Simulated 5% Packet Loss & 20ms Delay)
echo ""
echo "--> Test 4: Impairment via Lossy Proxy (Loss=5%, Delay=20ms)"
PORT=$((PORT + 1))
PROXY_PORT=$((PORT + 100))
rm -f "${RECV_FILE}"

./bin/receiver -p "${PORT}" -o "${RECV_DIR}" -q -1 &
RECV_PID=$!
sleep 0.1

./bin/proxy -l "${PROXY_PORT}" -p "${PORT}" -m 0.05 -t 20 -1 >/dev/null 2>&1 &
PROXY_PID=$!
sleep 0.2

./bin/sender -f "${SAMPLE_FILE}" -p "${PROXY_PORT}" -c 1024 -w 1 -t 1200 -q
wait "${RECV_PID}"
kill "${PROXY_PID}" 2>/dev/null || true
wait "${PROXY_PID}" 2>/dev/null || true

RECV_HASH=$(sha256sum "${RECV_FILE}" | awk '{print $1}')
if cmp -s "${SAMPLE_FILE}" "${RECV_FILE}"; then
    echo "  [PASS] Proxy Loss & Delay: Transmission recovered 100% through ARQ!"
    echo "         Original SHA-256: ${ORIGINAL_HASH}"
    echo "         Received SHA-256: ${RECV_HASH}"
else
    echo "  [FAIL] Proxy transmission failed!"
    exit 1
fi

echo ""
echo "========================================================"
echo " ALL END-TO-END TESTS PASSED SUCCESSFULLY!"
echo "========================================================"
