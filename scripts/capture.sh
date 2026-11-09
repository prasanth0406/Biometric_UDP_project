#!/usr/bin/env bash
# scripts/capture.sh - Real TCP Packet Capture to Wireshark PCAP
set -euo pipefail

PORT="${1:-5001}"
IFACE="${2:-lo}"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
OUT_PCAP="logs/capture_${TIMESTAMP}.pcap"

mkdir -p logs

echo "=========================================================="
echo " BMTP Wireshark Packet Capture"
echo "=========================================================="
echo " Port:       ${PORT}"
echo " Interface:  ${IFACE}"
echo " Output:     ${OUT_PCAP}"
echo " Filter:     tcp port ${PORT}"
echo "=========================================================="
echo "Press Ctrl+C to stop packet capture."

if command -v tcpdump &>/dev/null; then
    echo "[INFO] Using tcpdump..."
    exec tcpdump -i "${IFACE}" -w "${OUT_PCAP}" "tcp port ${PORT}"
elif command -v tshark &>/dev/null; then
    echo "[INFO] Using tshark..."
    exec tshark -i "${IFACE}" -f "tcp port ${PORT}" -w "${OUT_PCAP}"
elif [ -x "./bin/capture" ]; then
    echo "[INFO] Using compiled libpcap binary ./bin/capture..."
    exec ./bin/capture -i "${IFACE}" -p "${PORT}" -o "${OUT_PCAP}"
else
    echo "[ERROR] Neither tcpdump, tshark, nor ./bin/capture is available."
    exit 1
fi
