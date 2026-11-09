#!/usr/bin/env bash
# scripts/demo_capture.sh - Automates capture and analysis of a live transfer
set -euo pipefail

PORT=5001
PCAP="logs/capture_demo.pcap"

echo "=========================================================="
echo " Starting Background Packet Capture on port ${PORT}..."
echo "=========================================================="
sudo tcpdump -i lo -w "${PCAP}" "tcp port ${PORT}" 2>/dev/null &
TCPDUMP_PID=$!
sleep 0.5

echo "[CAPTURE] Starting Receiver on port ${PORT}..."
./bin/receiver -p "${PORT}" -o received_files -q -1 &
RECV_PID=$!
sleep 0.2

echo "[CAPTURE] Starting Sender with 10% fault injection..."
./bin/sender -f sample_data/biometric_template.dat -p "${PORT}" -c 1024 -w 1 -k 0.10 -q
wait "${RECV_PID}"

sleep 0.5
sudo kill -INT "${TCPDUMP_PID}" 2>/dev/null || true
wait "${TCPDUMP_PID}" 2>/dev/null || true

echo ""
echo "[ANALYSIS] Analyzing captured trace '${PCAP}' with tshark..."
bash scripts/analyze_pcap.sh "${PCAP}"
