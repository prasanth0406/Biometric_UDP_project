#!/usr/bin/env bash
# scripts/netem_teardown.sh - Remove Linux Netem Impairment Rules
set -euo pipefail

IFACE="${1:-lo}"

echo "=========================================================="
echo " Tearing down Linux Netem Impairment on '${IFACE}'"
echo "=========================================================="

sudo tc qdisc del dev "${IFACE}" root 2>/dev/null || true

echo "[SUCCESS] Impairment removed. Normal network restored."
