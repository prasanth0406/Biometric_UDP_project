#!/usr/bin/env bash
# scripts/netem_setup.sh - Linux Traffic Control (tc netem) Network Impairment Setup
set -euo pipefail

IFACE="${1:-lo}"
LOSS="${2:-5%}"
DELAY="${3:-50ms}"
RATE="${4:-10mbit}"

echo "=========================================================="
echo " Setting up Linux Netem Impairment on interface '${IFACE}'"
echo " Parameters: Loss=${LOSS}, Delay=${DELAY}, Rate=${RATE}"
echo "=========================================================="

# Remove existing root qdisc if present
sudo tc qdisc del dev "${IFACE}" root 2>/dev/null || true

# Add new netem qdisc
sudo tc qdisc add dev "${IFACE}" root netem loss "${LOSS}" delay "${DELAY}" rate "${RATE}"

echo "[SUCCESS] tc netem active. Current configuration:"
sudo tc qdisc show dev "${IFACE}"
