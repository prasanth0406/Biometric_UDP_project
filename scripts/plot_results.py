#!/usr/bin/env python3
"""
scripts/plot_results.py - Evaluation graph generator for BMTP benchmarks.
Supports matplotlib/pandas for PNG export, and provides a built-in zero-dependency
SVG renderer fallback when external python packages are not installed.
"""
import os
import sys
import csv

def generate_svg_chart(title, left_title, left_xlabel, left_ylabel, left_pts,
                       right_title, right_xlabel, right_ylabel, right_pts, out_file):
    """Generate a clean side-by-side two-panel SVG chart using standard Python."""
    w, h = 960, 420
    panel_w = 380
    panel_h = 260
    top_y = 70

    def render_panel(pts, px, py, pw, ph, ptitle, pxlabel, pylabel, color, is_bar=False):
        if not pts:
            return ""
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        y_errs = [p[2] for p in pts] if len(pts[0]) > 2 else [0.0] * len(pts)

        min_x = min(xs)
        max_x = max(xs)
        if min_x == max_x:
            min_x -= 1
            max_x += 1

        min_y = 0.0
        max_y = max(y + e for y, e in zip(ys, y_errs)) * 1.15
        if max_y == 0:
            max_y = 1.0

        def to_screen(x, y):
            sx = px + ((x - min_x) / (max_x - min_x)) * pw
            sy = (py + ph) - ((y - min_y) / (max_y - min_y)) * ph
            return sx, sy

        svg = []
        # Title
        svg.append(f'<text x="{px + pw/2}" y="{py - 20}" font-family="sans-serif" font-size="13" font-weight="bold" text-anchor="middle" fill="#2d3748">{ptitle}</text>')

        # Background & Grid
        svg.append(f'<rect x="{px}" y="{py}" width="{pw}" height="{ph}" fill="#f8fafc" stroke="#e2e8f0" stroke-width="1"/>')
        for i in range(5):
            gy = py + (ph / 4) * i
            val_y = max_y - (max_y / 4) * i
            svg.append(f'<line x1="{px}" y1="{gy}" x2="{px + pw}" y2="{gy}" stroke="#e2e8f0" stroke-dasharray="3,3"/>')
            svg.append(f'<text x="{px - 8}" y="{gy + 4}" font-family="sans-serif" font-size="10" text-anchor="end" fill="#64748b">{val_y:.1f}</text>')

        # Ticks on X
        for x, y in zip(xs, ys):
            sx, _ = to_screen(x, 0)
            svg.append(f'<line x1="{sx}" y1="{py + ph}" x2="{sx}" y2="{py + ph + 5}" stroke="#94a3b8"/>')
            svg.append(f'<text x="{sx}" y="{py + ph + 18}" font-family="sans-serif" font-size="10" text-anchor="middle" fill="#64748b">{x}</text>')

        # Axis labels
        svg.append(f'<text x="{px + pw/2}" y="{py + ph + 38}" font-family="sans-serif" font-size="11" text-anchor="middle" fill="#334155">{pxlabel}</text>')
        svg.append(f'<text x="{px - 45}" y="{py + ph/2}" font-family="sans-serif" font-size="11" text-anchor="middle" fill="#334155" transform="rotate(-90 {px - 45} {py + ph/2})">{pylabel}</text>')

        # Lines & points
        coords = [to_screen(x, y) for x, y in zip(xs, ys)]
        poly_pts = " ".join(f"{sx:.1f},{sy:.1f}" for sx, sy in coords)
        svg.append(f'<polyline points="{poly_pts}" fill="none" stroke="{color}" stroke-width="2.5"/>')

        for (x, y, err), (sx, sy) in zip(zip(xs, ys, y_errs), coords):
            if err > 0:
                _, sy_low = to_screen(x, max(0, y - err))
                _, sy_high = to_screen(x, y + err)
                svg.append(f'<line x1="{sx}" y1="{sy_low:.1f}" x2="{sx}" y2="{sy_high:.1f}" stroke="{color}" stroke-width="1.5"/>')
                svg.append(f'<line x1="{sx-3}" y1="{sy_low:.1f}" x2="{sx+3}" y2="{sy_low:.1f}" stroke="{color}" stroke-width="1.5"/>')
                svg.append(f'<line x1="{sx-3}" y1="{sy_high:.1f}" x2="{sx+3}" y2="{sy_high:.1f}" stroke="{color}" stroke-width="1.5"/>')
            svg.append(f'<circle cx="{sx:.1f}" cy="{sy:.1f}" r="4" fill="{color}" stroke="#ffffff" stroke-width="1.5"/>')

        return "\n".join(svg)

    svg_content = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}">',
        f'<rect width="{w}" height="{h}" fill="#ffffff"/>',
        f'<text x="{w/2}" y="32" font-family="sans-serif" font-size="17" font-weight="bold" text-anchor="middle" fill="#0f172a">{title}</text>',
        render_panel(left_pts, 80, top_y, panel_w, panel_h, left_title, left_xlabel, left_ylabel, '#2563eb'),
        render_panel(right_pts, 530, top_y, panel_w, panel_h, right_title, right_xlabel, right_ylabel, '#dc2626'),
        '</svg>'
    ]

    with open(out_file, 'w', encoding='utf-8') as f:
        f.write("\n".join(svg_content))

def main():
    csv_file = os.path.join("results", "results.csv")
    if not os.path.exists(csv_file):
        print(f"[ERROR] {csv_file} not found.")
        sys.exit(1)

    os.makedirs("results", exist_ok=True)

    try:
        import pandas as pd
        import matplotlib.pyplot as plt
        use_matplotlib = True
    except ImportError:
        use_matplotlib = False

    if use_matplotlib:
        df = pd.read_csv(csv_file)
        plt.style.use('seaborn-v0_8-whitegrid' if 'seaborn-v0_8-whitegrid' in plt.style.available else 'default')

        # 1. Chunk Size Evaluation Plot
        sub_a = df[df["Experiment"] == "Exp_A_Chunk_Size"].sort_values("Param_Value")
        if not sub_a.empty:
            fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(10, 4.5))
            fig.suptitle("Experiment A: Protocol Performance vs. Chunk Size", fontsize=12, fontweight='bold')
            ax1.errorbar(sub_a["Param_Value"], sub_a["Goodput_Mean_KBps"], yerr=sub_a["Goodput_Std_KBps"],
                         fmt='-o', color='#1f77b4', capsize=4, lw=2, label="Goodput")
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
            fig.suptitle("Experiment D: Go-Back-N Sliding Window Scaling", fontsize=12, fontweight='bold')
            ax1.errorbar(sub_d["Param_Value"], sub_d["Goodput_Mean_KBps"], yerr=sub_d["Goodput_Std_KBps"],
                         fmt='-o', color='#17becf', capsize=4, lw=2, label="Goodput (KB/s)")
            ax1.set_title("Goodput Scaling across Window Size")
            ax1.set_xlabel("Go-Back-N Window Size N (Default N=10)")
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

    else:
        # Zero-dependency SVG generation
        rows = []
        with open(csv_file, 'r', encoding='utf-8') as f:
            reader = csv.DictReader(f)
            for r in reader:
                rows.append(r)

        def get_series(exp_name, x_col, y_col, err_col):
            matched = [r for r in rows if r["Experiment"] == exp_name]
            matched.sort(key=lambda r: float(r[x_col]))
            return [(float(r[x_col]), float(r[y_col]), float(r[err_col])) for r in matched]

        # Exp A
        pts_a1 = get_series("Exp_A_Chunk_Size", "Param_Value", "Goodput_Mean_KBps", "Goodput_Std_KBps")
        pts_a2 = get_series("Exp_A_Chunk_Size", "Param_Value", "Overhead_Mean_pct", "Overhead_Std_pct")
        if pts_a1:
            generate_svg_chart("Experiment A: Protocol Performance vs. Chunk Size",
                               "Goodput vs. Chunk Size", "Chunk Size (bytes)", "Goodput (KB/s)", pts_a1,
                               "Header Overhead % vs. Chunk Size", "Chunk Size (bytes)", "Overhead (%)", pts_a2,
                               os.path.join("results", "exp_chunk_size.svg"))
            print("  -> Generated results/exp_chunk_size.svg")

        # Exp B
        pts_b1 = get_series("Exp_B_Loss_Rate", "Param_Value", "Retransmits_Mean", "Retransmits_Std")
        pts_b2 = get_series("Exp_B_Loss_Rate", "Param_Value", "Goodput_Mean_KBps", "Goodput_Std_KBps")
        if pts_b1:
            generate_svg_chart("Experiment B: Protocol Resilience under Packet Loss",
                               "Retransmissions vs. Loss Rate", "Loss Rate", "Retransmission Count", pts_b1,
                               "Goodput Degradation under Loss", "Loss Rate", "Goodput (KB/s)", pts_b2,
                               os.path.join("results", "exp_loss_rate.svg"))
            print("  -> Generated results/exp_loss_rate.svg")

        # Exp C
        pts_c1 = get_series("Exp_C_Delay", "Param_Value", "Duration_Mean_s", "Duration_Std_s")
        pts_c2 = get_series("Exp_C_Delay", "Param_Value", "RTT_Mean_ms", "RTT_Std_ms")
        if pts_c1:
            generate_svg_chart("Experiment C: Latency and RTT Evaluation",
                               "Total Transfer Time vs. Delay", "One-Way Latency (ms)", "Transfer Time (s)", pts_c1,
                               "Application RTT vs. Injected Delay", "One-Way Latency (ms)", "Measured RTT (ms)", pts_c2,
                               os.path.join("results", "exp_delay.svg"))
            print("  -> Generated results/exp_delay.svg")

        # Exp D
        pts_d1 = get_series("Exp_D_Window_Size", "Param_Value", "Goodput_Mean_KBps", "Goodput_Std_KBps")
        pts_d2 = get_series("Exp_D_Window_Size", "Param_Value", "Duration_Mean_s", "Duration_Std_s")
        if pts_d1:
            generate_svg_chart("Experiment D: Go-Back-N Sliding Window Scaling",
                               "Goodput Scaling vs. Window Size", "Go-Back-N Window Size N", "Goodput (KB/s)", pts_d1,
                               "Transfer Duration vs. Window Size", "Window Size N", "Transfer Time (s)", pts_d2,
                               os.path.join("results", "exp_window_size.svg"))
            print("  -> Generated results/exp_window_size.svg")

if __name__ == "__main__":
    main()
