#!/usr/bin/env bash
# scripts/demo_capture.sh - Automates capture and analysis of a live transfer
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${ROOT_DIR}"

mkdir -p logs received_files

PORT=5001
PROXY_PORT=5003
PCAP="logs/capture_demo.pcap"

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

BASE_NAME="$(basename "${SAMPLE_FILE}")"
FILE_SIZE=$(wc -c < "${SAMPLE_FILE}" | tr -d ' ')

echo "=========================================================="
echo " BMTP Packet Capture Demo with Real Biometric Data"
echo "=========================================================="
echo "[INFO] Biometric File: ${BASE_NAME} (${FILE_SIZE} bytes)"

USE_TCPDUMP=0
if command -v tcpdump &>/dev/null; then
    if sudo -n true 2>/dev/null; then
        USE_TCPDUMP=1
    fi
fi

if [ "${USE_TCPDUMP}" -eq 1 ]; then
    echo "[INFO] Starting background tcpdump capture on port ${PORT}..."
    sudo tcpdump -i lo -w "${PCAP}" "tcp port ${PORT}" 2>/dev/null &
    TCPDUMP_PID=$!
    sleep 0.5

    echo "[CAPTURE] Starting Receiver on port ${PORT}..."
    ./bin/receiver -p "${PORT}" -o received_files -q -1 &
    RECV_PID=$!
    sleep 0.2

    echo "[CAPTURE] Starting Sender with real biometric file (Go-Back-N Window=10)..."
    ./bin/sender -f "${SAMPLE_FILE}" -p "${PORT}" -c 4096 -w 10 -k 0.10 -q
    wait "${RECV_PID}"

    sleep 0.5
    sudo kill -INT "${TCPDUMP_PID}" 2>/dev/null || true
    wait "${TCPDUMP_PID}" 2>/dev/null || true
else
    echo "[INFO] Starting transparent Python PCAP recorder (port ${PROXY_PORT} -> ${PORT})..."
    python scripts/pcap_recorder.py "${PROXY_PORT}" "${PORT}" "${PCAP}" &
    RECORDER_PID=$!
    sleep 0.3

    echo "[CAPTURE] Starting Receiver on port ${PORT}..."
    ./bin/receiver -p "${PORT}" -o received_files -q -1 &
    RECV_PID=$!
    sleep 0.2

    echo "[CAPTURE] Starting Sender through recorder (port ${PROXY_PORT}) with Go-Back-N Window=10 and 10% fault injection..."
    ./bin/sender -f "${SAMPLE_FILE}" -p "${PROXY_PORT}" -c 4096 -w 10 -k 0.10 -q
    wait "${RECV_PID}"
    wait "${RECORDER_PID}" 2>/dev/null || true
fi

echo ""
echo "[INFO] Packet capture written to: ${PCAP}"
echo "[ANALYSIS] Analyzing captured trace '${PCAP}'..."
bash scripts/analyze_pcap.sh "${PCAP}"
