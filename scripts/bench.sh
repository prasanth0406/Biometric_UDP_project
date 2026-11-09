#!/usr/bin/env bash
# scripts/bench.sh - Automated Performance Evaluation and Benchmark Runner
set -euo pipefail

SAMPLE_FILE="sample_data/biometric_template.dat"
RECV_DIR="received_files"
RESULTS_DIR="results"
CSV_FILE="${RESULTS_DIR}/results.csv"

mkdir -p "${RECV_DIR}" "${RESULTS_DIR}" logs

# Header for results.csv
echo "Experiment,Param_Name,Param_Value,Duration_Mean_s,Duration_Std_s,Goodput_Mean_KBps,Goodput_Std_KBps,Throughput_Mean_KBps,Throughput_Std_KBps,Retransmits_Mean,Retransmits_Std,LossRate_Mean_pct,LossRate_Std_pct,Overhead_Mean_pct,Overhead_Std_pct,Handshake_Mean_ms,Handshake_Std_ms,RTT_Mean_ms,RTT_Std_ms" > "${CSV_FILE}"

BASE_PORT=7000

# Helper function to compute mean and sample standard deviation
calc_stats() {
    # Expects values on stdin, one per line
    awk '{
        vals[NR] = $1;
        sum += $1;
    }
    END {
        n = NR;
        if (n == 0) { print "0.0000 0.0000"; exit; }
        mean = sum / n;
        if (n == 1) { printf "%.4f 0.0000\n", mean; exit; }
        sq_diff = 0;
        for (i = 1; i <= n; i++) {
            sq_diff += (vals[i] - mean) ^ 2;
        }
        std = sqrt(sq_diff / (n - 1));
        printf "%.4f %.4f\n", mean, std;
    }'
}

run_experiment() {
    local exp_name="$1"
    local param_name="$2"
    local param_val="$3"
    local chunk_sz="$4"
    local win_sz="$5"
    local loss_rate="$6"
    local delay_ms="$7"
    local reps="${8:-5}"

    echo -n "  Testing ${param_name} = ${param_val} (${reps} runs): "

    local tmp_dur=$(mktemp)
    local tmp_gp=$(mktemp)
    local tmp_tp=$(mktemp)
    local tmp_ret=$(mktemp)
    local tmp_loss=$(mktemp)
    local tmp_ovh=$(mktemp)
    local tmp_hs=$(mktemp)
    local tmp_rtt=$(mktemp)

    for ((r=1; r<=reps; r++)); do
        BASE_PORT=$((BASE_PORT + 2))
        local r_port="${BASE_PORT}"
        local s_target_port="${r_port}"

        ./bin/receiver -p "${r_port}" -o "${RECV_DIR}" -q -1 &
        local r_pid=$!
        sleep 0.1

        local p_pid=""
        if (( $(echo "${loss_rate} > 0.0" | bc -l) )) || [ "${delay_ms}" -gt 0 ]; then
            local p_port=$((r_port + 1))
            ./bin/proxy -l "${p_port}" -p "${r_port}" -m "${loss_rate}" -t "${delay_ms}" -1 >/dev/null 2>&1 &
            p_pid=$!
            s_target_port="${p_port}"
            sleep 0.1
        fi

        local timeout_arg=$((500 + delay_ms * 3))
        local out
        out=$(./bin/sender -f "${SAMPLE_FILE}" -p "${s_target_port}" -c "${chunk_sz}" -w "${win_sz}" -t "${timeout_arg}" -q)
        wait "${r_pid}" 2>/dev/null || true
        if [ -n "${p_pid}" ]; then
            wait "${p_pid}" 2>/dev/null || true
        fi

        # Parse key=value fields from SUMMARY line
        local dur=$(echo "${out}" | grep "^SUMMARY:" | sed -n 's/.*duration=\([0-9.]*\).*/\1/p')
        local gp=$(echo "${out}" | grep "^SUMMARY:" | sed -n 's/.*goodput_kbps=\([0-9.]*\).*/\1/p')
        local tp=$(echo "${out}" | grep "^SUMMARY:" | sed -n 's/.*throughput_kbps=\([0-9.]*\).*/\1/p')
        local ret=$(echo "${out}" | grep "^SUMMARY:" | sed -n 's/.*retransmits=\([0-9.]*\).*/\1/p')
        local loss=$(echo "${out}" | grep "^SUMMARY:" | sed -n 's/.*loss_pct=\([0-9.]*\).*/\1/p')
        local ovh=$(echo "${out}" | grep "^SUMMARY:" | sed -n 's/.*overhead_pct=\([0-9.]*\).*/\1/p')
        local hs=$(echo "${out}" | grep "^SUMMARY:" | sed -n 's/.*handshake_ms=\([0-9.]*\).*/\1/p')
        local rtt=$(echo "${out}" | grep "^SUMMARY:" | sed -n 's/.*rtt_avg_ms=\([0-9.]*\).*/\1/p')

        echo "${dur:-0}" >> "${tmp_dur}"
        echo "${gp:-0}" >> "${tmp_gp}"
        echo "${tp:-0}" >> "${tmp_tp}"
        echo "${ret:-0}" >> "${tmp_ret}"
        echo "${loss:-0}" >> "${tmp_loss}"
        echo "${ovh:-0}" >> "${tmp_ovh}"
        echo "${hs:-0}" >> "${tmp_hs}"
        echo "${rtt:-0}" >> "${tmp_rtt}"
        echo -n "."
    done

    read -r dur_m dur_s < <(calc_stats < "${tmp_dur}")
    read -r gp_m gp_s < <(calc_stats < "${tmp_gp}")
    read -r tp_m tp_s < <(calc_stats < "${tmp_tp}")
    read -r ret_m ret_s < <(calc_stats < "${tmp_ret}")
    read -r loss_m loss_s < <(calc_stats < "${tmp_loss}")
    read -r ovh_m ovh_s < <(calc_stats < "${tmp_ovh}")
    read -r hs_m hs_s < <(calc_stats < "${tmp_hs}")
    read -r rtt_m rtt_s < <(calc_stats < "${tmp_rtt}")

    rm -f "${tmp_dur}" "${tmp_gp}" "${tmp_tp}" "${tmp_ret}" "${tmp_loss}" "${tmp_ovh}" "${tmp_hs}" "${tmp_rtt}"

    echo " Done! Mean Goodput: ${gp_m} KB/s, Time: ${dur_m}s, Retrans: ${ret_m}"
    echo "${exp_name},${param_name},${param_val},${dur_m},${dur_s},${gp_m},${gp_s},${tp_m},${tp_s},${ret_m},${ret_s},${loss_m},${loss_s},${ovh_m},${ovh_s},${hs_m},${hs_s},${rtt_m},${rtt_s}" >> "${CSV_FILE}"
}

echo "=========================================================="
echo " BMTP AUTOMATED BENCHMARK SUITE (5 Repetitions per config)"
echo "=========================================================="

# 1. Experiment A: Chunk Size (512, 1024, 4096, 8192)
echo ""
echo "[Experiment A] Chunk Size Evaluation (512, 1024, 4096, 8192)"
for csz in 512 1024 4096 8192; do
    run_experiment "Exp_A_Chunk_Size" "chunk_size" "${csz}" "${csz}" 1 0.0 0 5
done

# 2. Experiment B: Network Loss Rate (0%, 1%, 5%, 10%)
echo ""
echo "[Experiment B] Loss Rate Evaluation (0%, 1%, 5%, 10%)"
for loss in 0.00 0.01 0.05 0.10; do
    run_experiment "Exp_B_Loss_Rate" "loss_rate" "${loss}" 1024 1 "${loss}" 0 5
done

# 3. Experiment C: Network Latency (0, 50, 100 ms)
echo ""
echo "[Experiment C] Propagation Delay Evaluation (0ms, 50ms, 100ms)"
for delay in 0 50 100; do
    run_experiment "Exp_C_Delay" "delay_ms" "${delay}" 1024 1 0.00 "${delay}" 5
done

# 4. Experiment D: Stop-and-Wait vs Sliding Window (N=4, 8, 16)
echo ""
echo "[Experiment D] ARQ Protocol: Stop-and-Wait vs Sliding Window (N=4, 8, 16)"
for win in 1 4 8 16; do
    run_experiment "Exp_D_Window_Size" "window_size" "${win}" 1024 "${win}" 0.00 10 5
done

echo ""
echo "=========================================================="
echo " Generating Gnuplot Evaluation Graphs..."
echo "=========================================================="
if command -v gnuplot &>/dev/null; then
    gnuplot scripts/plot.gp
    echo "[SUCCESS] Graphs generated in ${RESULTS_DIR}/"
else
    echo "[WARN] Gnuplot not found. CSV results saved in ${CSV_FILE}"
fi

echo "Benchmark Complete! Consolidated dataset saved to ${CSV_FILE}"
