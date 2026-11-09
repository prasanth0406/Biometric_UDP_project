#!/usr/bin/env python3
"""
scripts/plot_results.py - Python alternative to plot.gp for generating benchmark charts
"""
import os
import sys
import pandas as pd
import matplotlib.pyplot as plt

def main():
    csv_file = os.path.join("results", "results.csv")
    if not os.path.exists(csv_file):
        print(f"[ERROR] {csv_file} not found.")
        sys.exit(1)

    df = pd.read_csv(csv_file)
    os.makedirs("results", exist_ok=True)

    plt.style.use('seaborn-v0_8-whitegrid' if 'seaborn-v0_8-whitegrid' in plt.style.available else 'default')

    # 1. Chunk Size Evaluation Plot
    sub_a = df[df["Experiment"] == "Exp_A_Chunk_Size"].sort_values("Param_Value")
    if not sub_a.empty:
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(10, 4.5))
        fig.suptitle("Experiment A: Protocol Performance vs. Chunk Size", fontsize=12, fontweight='bold')

        ax1.errorbar(sub_a["Param_Value"], sub_a["Goodput_Mean_KBps"], yerr=sub_a["Goodput_Std_KBps"],
                     fmt='-o', color='#1f77b4', capsize=4, lw=2, label="Goodput (mean ± std)")
        ax1.set_title("Effective Goodput vs. Chunk Size")
        ax1.set_xlabel("Chunk Size (bytes)")
        ax1.set_ylabel("Goodput (KB/s)")
        ax1.legend(loc='upper left')
        ax1.grid(True, linestyle='--', alpha=0.6)

        ax2.errorbar(sub_a["Param_Value"], sub_a["Overhead_Mean_pct"], yerr=sub_a["Overhead_Std_pct"],
                     fmt='-s', color='#d62728', capsize=4, lw=2, label="Overhead %")
        ax2.set_title("Framing Overhead % vs. Chunk Size")
        ax2.set_xlabel("Chunk Size (bytes)")
        ax2.set_ylabel("Header Overhead (%)")
        ax2.legend(loc='upper right')
        ax2.grid(True, linestyle='--', alpha=0.6)

        plt.tight_layout()
        plt.savefig(os.path.join("results", "exp_chunk_size.png"), dpi=150)
        plt.close()
        print("  -> Generated results/exp_chunk_size.png")

    # 2. Loss Rate Evaluation Plot
    sub_b = df[df["Experiment"] == "Exp_B_Loss_Rate"].sort_values("Param_Value")
    if not sub_b.empty:
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(10, 4.5))
        fig.suptitle("Experiment B: Protocol Resilience under Packet Loss", fontsize=12, fontweight='bold')

        ax1.errorbar(sub_b["Param_Value"], sub_b["Retransmits_Mean"], yerr=sub_b["Retransmits_Std"],
                     fmt='-^', color='#ff7f0e', capsize=4, lw=2, label="Retransmissions")
        ax1.set_title("Retransmissions vs. Network Loss Rate")
        ax1.set_xlabel("Simulated Loss Rate")
        ax1.set_ylabel("Retransmission Count")
        ax1.legend(loc='upper left')
        ax1.grid(True, linestyle='--', alpha=0.6)

        ax2.errorbar(sub_b["Param_Value"], sub_b["Goodput_Mean_KBps"], yerr=sub_b["Goodput_Std_KBps"],
                     fmt='-o', color='#2ca02c', capsize=4, lw=2, label="Goodput (KB/s)")
        ax2.set_title("Goodput Degradation under Loss")
        ax2.set_xlabel("Simulated Loss Rate")
        ax2.set_ylabel("Goodput (KB/s)")
        ax2.legend(loc='upper right')
        ax2.grid(True, linestyle='--', alpha=0.6)

        plt.tight_layout()
        plt.savefig(os.path.join("results", "exp_loss_rate.png"), dpi=150)
        plt.close()
        print("  -> Generated results/exp_loss_rate.png")

    # 3. Delay Evaluation Plot
    sub_c = df[df["Experiment"] == "Exp_C_Delay"].sort_values("Param_Value")
    if not sub_c.empty:
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(10, 4.5))
        fig.suptitle("Experiment C: Latency and RTT Evaluation", fontsize=12, fontweight='bold')

        ax1.errorbar(sub_c["Param_Value"], sub_c["Duration_Mean_s"], yerr=sub_c["Duration_Std_s"],
                     fmt='-o', color='#9467bd', capsize=4, lw=2, label="Transfer Time (s)")
        ax1.set_title("Total Transfer Time vs. Injected Delay")
        ax1.set_xlabel("One-Way Network Latency (ms)")
        ax1.set_ylabel("Transfer Duration (seconds)")
        ax1.legend(loc='upper left')
        ax1.grid(True, linestyle='--', alpha=0.6)

        ax2.errorbar(sub_c["Param_Value"], sub_c["RTT_Mean_ms"], yerr=sub_c["RTT_Std_ms"],
                     fmt='-s', color='#8c564b', capsize=4, lw=2, label="Measured RTT (ms)")
        ax2.set_title("Application RTT vs. Injected Delay")
        ax2.set_xlabel("One-Way Network Latency (ms)")
        ax2.set_ylabel("Measured RTT (ms)")
        ax2.legend(loc='upper left')
        ax2.grid(True, linestyle='--', alpha=0.6)

        plt.tight_layout()
        plt.savefig(os.path.join("results", "exp_delay.png"), dpi=150)
        plt.close()
        print("  -> Generated results/exp_delay.png")

    # 4. Window Size Evaluation Plot
    sub_d = df[df["Experiment"] == "Exp_D_Window_Size"].sort_values("Param_Value")
    if not sub_d.empty:
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(10, 4.5))
        fig.suptitle("Experiment D: Stop-and-Wait vs. Sliding Window", fontsize=12, fontweight='bold')

        ax1.errorbar(sub_d["Param_Value"], sub_d["Goodput_Mean_KBps"], yerr=sub_d["Goodput_Std_KBps"],
                     fmt='-o', color='#17becf', capsize=4, lw=2, label="Goodput (KB/s)")
        ax1.set_title("Goodput Scaling across Window Size")
        ax1.set_xlabel("Window Size N (N=1 is Stop-and-Wait)")
        ax1.set_ylabel("Goodput (KB/s)")
        ax1.legend(loc='lower right')
        ax1.grid(True, linestyle='--', alpha=0.6)

        ax2.errorbar(sub_d["Param_Value"], sub_d["Duration_Mean_s"], yerr=sub_d["Duration_Std_s"],
                     fmt='-s', color='#e377c2', capsize=4, lw=2, label="Transfer Time (s)")
        ax2.set_title("Transfer Duration across Window Size")
        ax2.set_xlabel("Window Size N")
        ax2.set_ylabel("Transfer Time (seconds)")
        ax2.legend(loc='upper right')
        ax2.grid(True, linestyle='--', alpha=0.6)

        plt.tight_layout()
        plt.savefig(os.path.join("results", "exp_window_size.png"), dpi=150)
        plt.close()
        print("  -> Generated results/exp_window_size.png")

if __name__ == "__main__":
    main()
