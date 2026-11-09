#!/usr/bin/env bash
# scripts/analyze_pcap.sh - Automated PCAP Wireshark Trace Analysis
set -euo pipefail

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
else
    # Fallback using capinfos if available or tcpdump
    if command -v capinfos &>/dev/null; then
        capinfos "${PCAP_FILE}"
    elif command -v tcpdump &>/dev/null; then
        echo "[INFO] Running basic packet summary with tcpdump..."
        tcpdump -r "${PCAP_FILE}" -nn -q | head -n 30
        echo "... [Run 'wireshark ${PCAP_FILE}' for full GUI analysis]"
    else
        echo "[INFO] Install tshark ('sudo apt-get install tshark') for automated metrics extraction."
        echo "[INFO] You can open '${PCAP_FILE}' directly in Wireshark GUI."
    fi
fi
