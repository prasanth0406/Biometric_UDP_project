#!/usr/bin/env bash
# scripts/test_e2e.sh - End-to-End Localhost Integration Tests
set -euo pipefail

PORT=5400
SAMPLE_FILE="sample_data/biometric_template.dat"
RECV_DIR="received_files"
BASE_NAME="biometric_template.dat"
RECV_FILE="${RECV_DIR}/${BASE_NAME}"

mkdir -p "${RECV_DIR}" logs results

echo "========================================================"
echo " Starting End-to-End Localhost Verification"
echo "========================================================"

ORIGINAL_HASH=$(sha256sum "${SAMPLE_FILE}" | awk '{print $1}')
echo "[TEST] Original Biometric File Hash: ${ORIGINAL_HASH}"

# Test 1: Stop-and-Wait (N=1)
echo ""
echo "--> Test 1: Stop-and-Wait Mode (Chunk=1024, Window=1)"
PORT=$((PORT + 1))
rm -f "${RECV_FILE}"
./bin/receiver -p "${PORT}" -o "${RECV_DIR}" -q -1 &
RECV_PID=$!
sleep 0.2

./bin/sender -f "${SAMPLE_FILE}" -p "${PORT}" -c 1024 -w 1 -q
wait "${RECV_PID}"

if cmp -s "${SAMPLE_FILE}" "${RECV_FILE}"; then
    echo "  [PASS] Stop-and-Wait: Reconstructed file bit-for-bit identical!"
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
sleep 0.2

./bin/sender -f "${SAMPLE_FILE}" -p "${PORT}" -c 2048 -w 4 -q
wait "${RECV_PID}"

if cmp -s "${SAMPLE_FILE}" "${RECV_FILE}"; then
    echo "  [PASS] Sliding Window (N=4): Reconstructed file bit-for-bit identical!"
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
sleep 0.2

./bin/sender -f "${SAMPLE_FILE}" -p "${PORT}" -c 1024 -w 1 -k 0.15 -q
wait "${RECV_PID}"

if cmp -s "${SAMPLE_FILE}" "${RECV_FILE}"; then
    echo "  [PASS] Fault Injection: CRC mismatch detected, NACK/Retransmit recovered file!"
else
    echo "  [FAIL] Fault Injection: File recovery failed!"
    exit 1
fi

# Test 4: Impairment Proxy (Simulated 5% Packet Loss & 20ms Delay)
echo ""
echo "--> Test 4: Impairment via Lossy Proxy (Loss=5%, Delay=15ms)"
PORT=$((PORT + 1))
PROXY_PORT=$((PORT + 100))
rm -f "${RECV_FILE}"

./bin/receiver -p "${PORT}" -o "${RECV_DIR}" -q -1 &
RECV_PID=$!
sleep 0.1

./bin/proxy -l "${PROXY_PORT}" -p "${PORT}" -m 0.05 -t 15 -1 >/dev/null 2>&1 &
PROXY_PID=$!
sleep 0.2

./bin/sender -f "${SAMPLE_FILE}" -p "${PROXY_PORT}" -c 1024 -w 1 -t 800 -q
wait "${RECV_PID}"
wait "${PROXY_PID}" 2>/dev/null || true

if cmp -s "${SAMPLE_FILE}" "${RECV_FILE}"; then
    echo "  [PASS] Proxy Loss & Delay: Transmission recovered 100% through ARQ!"
else
    echo "  [FAIL] Proxy transmission failed!"
    exit 1
fi

echo ""
echo "========================================================"
echo " ALL END-TO-END TESTS PASSED SUCCESSFULLY!"
echo "========================================================"
