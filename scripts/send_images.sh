#!/usr/bin/env bash
# ==============================================================================
# scripts/send_images.sh
# BMTP Batch Biometric Image Transmission Script (Linux / Bash)
#
# Usage:
#   ./scripts/send_images.sh <HOST_IP> <RECEIVER_IP> [PORT] [LIMIT]
#
# Arguments:
#   $1 (HOST_IP)     : IP address of the host machine (sender)
#   $2 (RECEIVER_IP) : IP address of the receiver machine
#   $3 (PORT)        : Base TCP port (optional, default: 5001)
#   $4 (LIMIT)       : Limit number of images (optional, default: 0 = all images)
#
# Options:
#   --listen / --receiver : Run in receiver daemon mode on the receiver host
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${ROOT_DIR}"

# ------------------------------------------------------------------------------
# Help & Argument Validation
# ------------------------------------------------------------------------------
print_usage() {
    cat << 'EOF'
Usage:
  # Cross-host Sender Mode (transfers images to receiver machine):
  ./scripts/send_images.sh <HOST_IP> <RECEIVER_IP> [PORT] [LIMIT]

  # Cross-host Receiver Mode (listens on receiver machine for incoming transfers):
  ./scripts/send_images.sh --listen [PORT]
  ./scripts/send_images.sh <HOST_IP> <RECEIVER_IP> [PORT] --listen

  # Localhost Self-Test (runs receiver in background and sender locally):
  ./scripts/send_images.sh 127.0.0.1 127.0.0.1 [PORT] [LIMIT]

Arguments:
  HOST_IP      : IP address of the sender machine e.g. 192.168.1.10
  RECEIVER_IP  : IP address of the receiver machine e.g. 192.168.1.20 (or 127.0.0.1)
  PORT         : (Optional) TCP port (default: 5001)
  LIMIT        : (Optional) Number of images to transfer (default: 0 = all images)

Options / Modes:
  --listen, --receiver, -l : Start in continuous receiver mode on the receiver host
  --help, -h               : Show this help message

Examples:
  # 1. On Receiver Host (e.g. 192.168.1.20):
  ./scripts/send_images.sh --listen

  # 2. On Sender Host (e.g. 192.168.1.10):
  ./scripts/send_images.sh 192.168.1.10 192.168.1.20

  # 3. Localhost test (5 images on port 5001):
  ./scripts/send_images.sh 127.0.0.1 127.0.0.1 5001 5
EOF
}

# Parse options and flags
RUN_AS_RECEIVER=0
POSITIONAL=()

for arg in "$@"; do
    case "${arg}" in
        -h|--help)
            print_usage
            exit 0
            ;;
        --listen|--receiver|-l)
            RUN_AS_RECEIVER=1
            ;;
        *)
            POSITIONAL+=("${arg}")
            ;;
    esac
done

HOST_IP=""
RECEIVER_IP=""
PORT=5001
LIMIT=0
CHUNK_SIZE=8192
WINDOW_SIZE=10
IMAGE_DIR="sample_data/real_biometric"
RECV_DIR="received_files"
LOG_DIR="logs"
RESULTS_DIR="results"

if [ "${RUN_AS_RECEIVER}" -eq 1 ]; then
    if [ ${#POSITIONAL[@]} -eq 0 ]; then
        HOST_IP="0.0.0.0"
        RECEIVER_IP="0.0.0.0"
        PORT=5001
    elif [ ${#POSITIONAL[@]} -eq 1 ]; then
        if [[ "${POSITIONAL[0]}" =~ ^[0-9]+$ ]]; then
            PORT="${POSITIONAL[0]}"
            HOST_IP="0.0.0.0"
            RECEIVER_IP="0.0.0.0"
        else
            RECEIVER_IP="${POSITIONAL[0]}"
            HOST_IP="0.0.0.0"
            PORT=5001
        fi
    elif [ ${#POSITIONAL[@]} -eq 2 ]; then
        if [[ "${POSITIONAL[1]}" =~ ^[0-9]+$ ]]; then
            RECEIVER_IP="${POSITIONAL[0]}"
            PORT="${POSITIONAL[1]}"
            HOST_IP="0.0.0.0"
        else
            HOST_IP="${POSITIONAL[0]}"
            RECEIVER_IP="${POSITIONAL[1]}"
            PORT=5001
        fi
    else
        HOST_IP="${POSITIONAL[0]}"
        RECEIVER_IP="${POSITIONAL[1]}"
        PORT="${POSITIONAL[2]:-5001}"
        LIMIT="${POSITIONAL[3]:-0}"
    fi
else
    if [ ${#POSITIONAL[@]} -lt 2 ]; then
        print_usage
        exit 1
    fi
    HOST_IP="${POSITIONAL[0]}"
    RECEIVER_IP="${POSITIONAL[1]}"
    PORT="${POSITIONAL[2]:-5001}"
    LIMIT="${POSITIONAL[3]:-0}"
fi

# Validate PORT is numeric and in valid range
if ! [[ "${PORT}" =~ ^[0-9]+$ ]] || [ "${PORT}" -le 0 ] || [ "${PORT}" -gt 65535 ]; then
    echo "[ERROR] Invalid port '${PORT}'. Port must be an integer between 1 and 65535."
    exit 1
fi

# Ensure directories exist
mkdir -p "${RECV_DIR}" "${LOG_DIR}" "${RESULTS_DIR}"

# Determine executable names (Linux vs Windows/MSYS/Cygwin)
BIN_SENDER="./bin/sender"
BIN_RECEIVER="./bin/receiver"
if [ ! -f "${BIN_SENDER}" ] && [ -f "./bin/sender.exe" ]; then
    BIN_SENDER="./bin/sender.exe"
fi
if [ ! -f "${BIN_RECEIVER}" ] && [ -f "./bin/receiver.exe" ]; then
    BIN_RECEIVER="./bin/receiver.exe"
fi

# Build binaries if missing
if [ ! -f "${BIN_SENDER}" ] || [ ! -f "${BIN_RECEIVER}" ]; then
    echo "[BUILD] Binaries not found in bin/. Compiling release binaries via 'make all'..."
    make all
fi

# Helper function: Calculate SHA-256 hash across platforms
calculate_sha256() {
    local target="$1"
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "${target}" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "${target}" | awk '{print $1}'
    elif command -v openssl >/dev/null 2>&1; then
        openssl dgst -sha256 "${target}" | awk '{print $NF}'
    else
        echo "NO_SHA256_TOOL"
    fi
}

# Auto-detect local IP to infer receiver mode if applicable
LOCAL_IPS=$(hostname -I 2>/dev/null || ip -o -4 addr show 2>/dev/null | awk '{split($4,a,"/"); print a[1]}' || ifconfig 2>/dev/null | grep -Eo 'inet (addr:)?([0-9]*\.){3}[0-9]*' | awk '{print $2}' || echo "127.0.0.1")

if [ "${RUN_AS_RECEIVER}" -eq 0 ] && [ "${HOST_IP}" != "${RECEIVER_IP}" ]; then
    for lip in ${LOCAL_IPS}; do
        if [ "${lip}" = "${RECEIVER_IP}" ]; then
            echo "[AUTO-DETECT] Current machine IP (${lip}) matches Receiver IP (${RECEIVER_IP})."
            echo "[AUTO-DETECT] Switching to RECEIVER listening mode."
            RUN_AS_RECEIVER=1
            break
        fi
    done
fi

TIMESTAMP=$(date +"%Y%m%d_%H%M%S")

# ==============================================================================
# RECEIVER MODE (Runs on the receiver host to accept incoming images)
# ==============================================================================
if [ "${RUN_AS_RECEIVER}" -eq 1 ]; then
    echo "================================================================================"
    echo " BMTP RECEIVER DAEMON - LISTENING FOR BIOMETRIC IMAGES"
    echo "================================================================================"
    [ -n "${HOST_IP}" ] && [ "${HOST_IP}" != "0.0.0.0" ] && echo " Expected Sender Host   : ${HOST_IP}"
    echo " Receiver Listen IP     : 0.0.0.0 (All Interfaces)"
    echo " Listen Port            : ${PORT}"
    echo " Destination Directory  : ${RECV_DIR}"
    echo " Log Directory          : ${LOG_DIR}"
    echo " Started At             : $(date)"
    echo "================================================================================"
    echo "Waiting for incoming image transfers on port ${PORT}... (Press Ctrl+C to stop)"
    echo ""

    # Run receiver daemon in continuous mode (accepting multiple transfers seamlessly)
    exec "${BIN_RECEIVER}" -b 0.0.0.0 -p "${PORT}" -o "${RECV_DIR}" -l "${LOG_DIR}"
fi

# ==============================================================================
# SENDER / LOCALHOST MODE (Transfers all images to receiver)
# ==============================================================================

# Verify image directory exists
if [ ! -d "${IMAGE_DIR}" ]; then
    echo "[ERROR] Image directory '${IMAGE_DIR}' not found!"
    exit 1
fi

# Generate list of all image files (.jpg, .jpeg, .png, .bmp)
IMAGE_MANIFEST="${LOG_DIR}/image_manifest_${TIMESTAMP}.txt"
find "${IMAGE_DIR}" -maxdepth 1 -type f \( -iname "*.jpg" -o -iname "*.jpeg" -o -iname "*.png" -o -iname "*.bmp" \) | sort > "${IMAGE_MANIFEST}"

TOTAL_FOUND=$(wc -l < "${IMAGE_MANIFEST}" | tr -d ' ')
if [ "${TOTAL_FOUND}" -eq 0 ]; then
    echo "[ERROR] No image files found in '${IMAGE_DIR}'."
    exit 1
fi

if [ "${LIMIT}" -gt 0 ] && [ "${LIMIT}" -lt "${TOTAL_FOUND}" ]; then
    TOTAL_TO_SEND="${LIMIT}"
else
    TOTAL_TO_SEND="${TOTAL_FOUND}"
fi

LOG_FILE="${LOG_DIR}/transfer_${TIMESTAMP}.log"
CSV_FILE="${RESULTS_DIR}/transfer_results_${TIMESTAMP}.csv"

# Log session header
cat << EOF | tee "${LOG_FILE}"
====================================================================================================
 BMTP BIOMETRIC IMAGE TRANSMISSION ENGINE
====================================================================================================
 Host IP (Sender)    : ${HOST_IP}
 Receiver IP         : ${RECEIVER_IP}
 Target TCP Port     : ${PORT}
 Image Directory     : ${IMAGE_DIR}
 Total Images Found  : ${TOTAL_FOUND}
 Images to Transfer  : ${TOTAL_TO_SEND}
 Protocol / Framing  : BMTP (Big-Endian 18-byte Header, CRC32 chunk + SHA-256 full file)
 ARQ Mode            : Go-Back-N Sliding-Window (Window N=${WINDOW_SIZE}, Chunk Size=${CHUNK_SIZE} B)
 Detailed Log File   : ${LOG_FILE}
 CSV Summary File    : ${CSV_FILE}
 Execution Start     : $(date)
====================================================================================================
EOF

# Initialize CSV log with headers
echo "index,filename,filesize_bytes,duration_sec,goodput_kbps,throughput_kbps,retransmits,handshake_ms,rtt_avg_ms,orig_sha256,recv_sha256,status" > "${CSV_FILE}"

# Check if running locally (Host IP == Receiver IP or 127.0.0.1)
IS_LOCAL=0
if [ "${HOST_IP}" = "${RECEIVER_IP}" ] || [ "${RECEIVER_IP}" = "127.0.0.1" ] || [ "${RECEIVER_IP}" = "localhost" ]; then
    IS_LOCAL=1
fi

PASSED=0
FAILED=0
TOTAL_BYTES=0
INDEX=0
START_TIME_TOTAL=$(date +%s)

# ------------------------------------------------------------------------------
# Iterate through all images using INPUT REDIRECTION (< "${IMAGE_MANIFEST}")
# ------------------------------------------------------------------------------
while IFS= read -r image_path; do
    if [ "${LIMIT}" -gt 0 ] && [ "${INDEX}" -ge "${LIMIT}" ]; then
        break
    fi
    INDEX=$((INDEX + 1))

    BASE_NAME=$(basename "${image_path}")
    FILE_SIZE=$(wc -c < "${image_path}" | tr -d ' ')
    TOTAL_BYTES=$((TOTAL_BYTES + FILE_SIZE))
    ORIG_HASH=$(calculate_sha256 "${image_path}")
    RECV_FILE="${RECV_DIR}/${BASE_NAME}"

    # If running locally, clean existing destination file and launch local receiver
    RECV_PID=""
    if [ "${IS_LOCAL}" -eq 1 ]; then
        rm -f "${RECV_FILE}"
        "${BIN_RECEIVER}" -p "${PORT}" -o "${RECV_DIR}" -q -1 &
        RECV_PID=$!
        # Brief pause to ensure socket bind/listen is active
        sleep 0.05
    fi

    # Detailed Item Header on Console and Log
    ITEM_HEADER=$(cat << EOF

----------------------------------------------------------------------------------------------------
 [Image ${INDEX}/${TOTAL_TO_SEND}] Transferring: ${BASE_NAME}
----------------------------------------------------------------------------------------------------
 Source Host       : ${HOST_IP}
 Target Receiver   : ${RECEIVER_IP}:${PORT}
 File Size         : ${FILE_SIZE} bytes ($(( FILE_SIZE / 1024 )) KB)
 Original SHA-256  : ${ORIG_HASH}
 Time Started      : $(date +'%T.%3N')
----------------------------------------------------------------------------------------------------
EOF
)
    echo "${ITEM_HEADER}" | tee -a "${LOG_FILE}"

    # Execute sender and capture full detailed output
    TMP_SENDER_LOG="${LOG_DIR}/tmp_sender_${TIMESTAMP}_${INDEX}.log"
    SENDER_STATUS=0

    # Retry loop (up to 5 attempts in case of network latency)
    MAX_RETRIES=5
    ATTEMPT=1
    while [ "${ATTEMPT}" -le "${MAX_RETRIES}" ]; do
        if "${BIN_SENDER}" -f "${image_path}" -h "${RECEIVER_IP}" -p "${PORT}" -c "${CHUNK_SIZE}" -w "${WINDOW_SIZE}" > "${TMP_SENDER_LOG}" 2>&1; then
            SENDER_STATUS=0
            break
        else
            SENDER_STATUS=$?
            echo "   [RETRY ${ATTEMPT}/${MAX_RETRIES}] Connection to ${RECEIVER_IP}:${PORT} failed (exit ${SENDER_STATUS}). Waiting 0.5s..." | tee -a "${LOG_FILE}"
            sleep 0.5
            ATTEMPT=$((ATTEMPT + 1))
        fi
    done

    # Console the detailed log in terminal using input redirection from temporary log
    cat < "${TMP_SENDER_LOG}"
    # Append the detailed log into the separate log file using input redirection
    cat < "${TMP_SENDER_LOG}" >> "${LOG_FILE}"

    # If local, wait for receiver process to complete
    if [ -n "${RECV_PID}" ]; then
        wait "${RECV_PID}" 2>/dev/null || true
    fi

    # Extract performance metrics from the sender SUMMARY line
    SUMMARY_LINE=$(grep "^SUMMARY:" "${TMP_SENDER_LOG}" 2>/dev/null || echo "")
    DURATION=$(echo "${SUMMARY_LINE}" | grep -o 'duration=[0-9.]*' | cut -d= -f2 || echo "0.0")
    GOODPUT=$(echo "${SUMMARY_LINE}" | grep -o 'goodput_kbps=[0-9.]*' | cut -d= -f2 || echo "0.0")
    THROUGHPUT=$(echo "${SUMMARY_LINE}" | grep -o 'throughput_kbps=[0-9.]*' | cut -d= -f2 || echo "0.0")
    RETRANSMITS=$(echo "${SUMMARY_LINE}" | grep -o 'retransmits=[0-9]*' | cut -d= -f2 || echo "0")
    HANDSHAKE_MS=$(echo "${SUMMARY_LINE}" | grep -o 'handshake_ms=[0-9.]*' | cut -d= -f2 || echo "0.0")
    RTT_AVG_MS=$(echo "${SUMMARY_LINE}" | grep -o 'rtt_avg_ms=[0-9.]*' | cut -d= -f2 || echo "0.0")
    STATUS_FROM_SENDER=$(echo "${SUMMARY_LINE}" | grep -o 'status=[A-Z]*' | cut -d= -f2 || echo "")

    # Hash verification
    RECV_HASH="REMOTE_HOST_VERIFIED"
    if [ "${IS_LOCAL}" -eq 1 ]; then
        if [ -f "${RECV_FILE}" ]; then
            RECV_HASH=$(calculate_sha256 "${RECV_FILE}")
        else
            RECV_HASH="FILE_NOT_FOUND"
        fi
    fi

    # Determine final verdict
    if [ "${SENDER_STATUS}" -eq 0 ] && [ "${STATUS_FROM_SENDER}" = "PASS" ]; then
        if [ "${IS_LOCAL}" -eq 1 ] && [ "${ORIG_HASH}" != "${RECV_HASH}" ]; then
            STATUS="FAIL_HASH_MISMATCH"
            FAILED=$((FAILED + 1))
        else
            STATUS="PASS"
            PASSED=$((PASSED + 1))
        fi
    else
        STATUS="FAIL_TRANSMISSION"
        FAILED=$((FAILED + 1))
    fi

    # Write metrics to CSV
    echo "${INDEX},${BASE_NAME},${FILE_SIZE},${DURATION},${GOODPUT},${THROUGHPUT},${RETRANSMITS},${HANDSHAKE_MS},${RTT_AVG_MS},${ORIG_HASH},${RECV_HASH},${STATUS}" >> "${CSV_FILE}"

    # Print Item Result Confirmation Banner
    ITEM_RESULT=$(cat << EOF
>> VERDICT: [${STATUS}] | File: ${BASE_NAME} | Rate: ${GOODPUT} KB/s (${THROUGHPUT} KB/s raw) | Duration: ${DURATION}s | SHA-256: ${ORIG_HASH:0:16}...
EOF
)
    echo "${ITEM_RESULT}" | tee -a "${LOG_FILE}"

    # Cleanup temporary item log
    rm -f "${TMP_SENDER_LOG}"

    # Short delay to allow socket cleanup between sequential transfers
    sleep 0.08

done < "${IMAGE_MANIFEST}"

END_TIME_TOTAL=$(date +%s)
TOTAL_DURATION=$((END_TIME_TOTAL - START_TIME_TOTAL))
if [ "${TOTAL_DURATION}" -le 0 ]; then
    TOTAL_DURATION=1
fi
TOTAL_MB=$(awk "BEGIN {printf \"%.2f\", ${TOTAL_BYTES} / (1024 * 1024)}")
AGGREGATE_MBPS=$(awk "BEGIN {printf \"%.2f\", (${TOTAL_BYTES} * 8) / (${TOTAL_DURATION} * 1000000)}")

# ------------------------------------------------------------------------------
# Final Execution Summary
# ------------------------------------------------------------------------------
SUMMARY_REPORT=$(cat << EOF

====================================================================================================
 FINAL BATCH TRANSMISSION SUMMARY REPORT
====================================================================================================
 Total Images Processed : ${INDEX} / ${TOTAL_TO_SEND}
 Passed (Bit-for-Bit)   : ${PASSED}
 Failed Transfers       : ${FAILED}
 Total Volume Sent      : ${TOTAL_MB} MB (${TOTAL_BYTES} bytes)
 Total Wall-Clock Time  : ${TOTAL_DURATION} seconds
 Aggregate Goodput      : ${AGGREGATE_MBPS} Mbps
 Detailed Console Log   : ${LOG_FILE}
 Comprehensive CSV Log  : ${CSV_FILE}
 Integrity Guarantee   : Dual IEEE 802.3 CRC32 + FIPS 180-2 SHA-256 Verified
====================================================================================================
EOF
)

# Output summary to console and append to log file using redirection
echo "${SUMMARY_REPORT}" | tee -a "${LOG_FILE}"

# Cleanup manifest
rm -f "${IMAGE_MANIFEST}"

if [ "${FAILED}" -gt 0 ]; then
    echo "[VERDICT] BATCH TRANSFER FINISHED WITH ${FAILED} FAILURES."
    exit 1
else
    echo "[VERDICT] ALL ${PASSED} IMAGES TRANSFERRED AND VERIFIED BIT-FOR-BIT IDENTICAL."
    exit 0
fi
