#!/usr/bin/env bash
# scripts/analyze_pcap.sh - Automated PCAP Wireshark Trace Analysis
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${ROOT_DIR}"

PCAP_FILE="${1:-}"

if [ -z "${PCAP_FILE}" ] || [ ! -f "${PCAP_FILE}" ]; then
    echo "Usage: $0 <capture.pcap>"
    exit 1
fi

echo "=========================================================="
echo " PCAP Wireshark Trace Analysis: ${PCAP_FILE}"
echo "=========================================================="

if command -v tshark &>/dev/null; then
    TOTAL_PKTS=$(tshark -r "${PCAP_FILE}" | wc -l)
    TCP_PKTS=$(tshark -r "${PCAP_FILE}" -Y "tcp" | wc -l)
    SYN_PKTS=$(tshark -r "${PCAP_FILE}" -Y "tcp.flags.syn==1" | wc -l)
    FIN_PKTS=$(tshark -r "${PCAP_FILE}" -Y "tcp.flags.fin==1" | wc -l)
    RETRANS=$(tshark -r "${PCAP_FILE}" -Y "tcp.analysis.retransmission" 2>/dev/null | wc -l)
    DUP_ACKS=$(tshark -r "${PCAP_FILE}" -Y "tcp.analysis.duplicate_ack" 2>/dev/null | wc -l)

    START_TIME=$(tshark -r "${PCAP_FILE}" -T fields -e frame.time_epoch | head -n 1)
    END_TIME=$(tshark -r "${PCAP_FILE}" -T fields -e frame.time_epoch | tail -n 1)
    DURATION=$(awk "BEGIN {print ${END_TIME} - ${START_TIME}}")
    TOTAL_BYTES=$(tshark -r "${PCAP_FILE}" -T fields -e frame.len | awk '{s+=$1} END {print s}')

    THROUGHPUT_KBPS=$(awk "BEGIN {if (${DURATION}>0) print (${TOTAL_BYTES}/1024.0)/${DURATION}; else print 0}")
    THROUGHPUT_MBPS=$(awk "BEGIN {print (${THROUGHPUT_KBPS}*8)/1000.0}")

    echo " Total Packets:        ${TOTAL_PKTS}"
    echo " TCP Frames:           ${TCP_PKTS}"
    echo " SYN (Handshakes):     ${SYN_PKTS}"
    echo " FIN (Teardowns):      ${FIN_PKTS}"
    echo " Retransmissions:      ${RETRANS}"
    echo " Duplicate ACKs:       ${DUP_ACKS}"
    echo " Duration:             ${DURATION} s"
    echo " Total Wire Bytes:     ${TOTAL_BYTES} bytes"
    echo " Wire Throughput:      ${THROUGHPUT_KBPS} KB/s (${THROUGHPUT_MBPS} Mbps)"
    echo "=========================================================="
elif command -v python &>/dev/null; then
    python - "${PCAP_FILE}" << 'PYEOF'
import sys, struct

pcap_file = sys.argv[1]
with open(pcap_file, 'rb') as f:
    ghdr = f.read(24)
    if len(ghdr) < 24:
        sys.exit(0)
    magic = struct.unpack('!I', ghdr[:4])[0]
    endian = '!' if magic == 0xa1b2c3d4 else '<'

    pkts = 0
    total_bytes = 0
    syn_count = 0
    fin_count = 0
    first_ts = None
    last_ts = None

    while True:
        phdr = f.read(16)
        if len(phdr) < 16:
            break
        sec, usec, incl_len, orig_len = struct.unpack(f'{endian}IIII', phdr)
        ts = sec + usec / 1000000.0
        if first_ts is None:
            first_ts = ts
        last_ts = ts

        data = f.read(incl_len)
        pkts += 1
        total_bytes += incl_len

        # Parse Eth (14) + IP (20) + TCP (20)
        if len(data) >= 54:
            proto = data[23]
            if proto == 6:  # TCP
                tcp_flags = data[47]
                if tcp_flags & 0x02:
                    syn_count += 1
                if tcp_flags & 0x01:
                    fin_count += 1

    dur = (last_ts - first_ts) if (first_ts and last_ts) else 0.0
    tp_kbps = (total_bytes / 1024.0 / dur) if dur > 0 else 0.0
    tp_mbps = (tp_kbps * 8) / 1000.0

    print(f" Total Packets:        {pkts}")
    print(f" TCP Frames:           {pkts}")
    print(f" SYN (Handshakes):     {syn_count}")
    print(f" FIN (Teardowns):      {fin_count}")
    print(f" Duration:             {dur:.4f} s")
    print(f" Total Wire Bytes:     {total_bytes} bytes")
    print(f" Wire Throughput:      {tp_kbps:.2f} KB/s ({tp_mbps:.2f} Mbps)")
    print("==========================================================")
PYEOF
else
    echo "[INFO] Install tshark or Python for automated metrics extraction."
    echo "[INFO] You can open '${PCAP_FILE}' directly in Wireshark GUI."
fi
