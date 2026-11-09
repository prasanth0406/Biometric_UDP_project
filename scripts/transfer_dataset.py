#!/usr/bin/env python3
"""
scripts/transfer_dataset.py
Transfers actual Part A biometric dataset files using the BMTP sender and receiver,
records transmission duration, throughput, and compares original vs reconstructed SHA-256 hashes.
"""

import os
import sys
import time
import hashlib
import subprocess
import argparse

def main():
    parser = argparse.ArgumentParser(description="BMTP Real Biometric Dataset Batch Transfer")
    parser.add_argument("--limit", type=int, default=0, help="Maximum number of files to transfer (0 for all)")
    parser.add_argument("--base-port", type=int, default=7400, help="Base TCP port")
    parser.add_argument("--chunk-size", type=int, default=8192, help="BMTP chunk size")
    parser.add_argument("--window", type=int, default=16, help="Sliding window size")
    parser.add_argument("--output-csv", type=str, default="results/dataset_transfer_results.csv", help="CSV output path")
    args = parser.parse_args()

    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    real_dir = os.path.join(project_root, "sample_data", "real_biometric")
    recv_dir = os.path.join(project_root, "received_files")
    bin_receiver = os.path.join(project_root, "bin", "receiver.exe")
    bin_sender = os.path.join(project_root, "bin", "sender.exe")

    if not os.path.exists(bin_receiver):
        bin_receiver = os.path.join(project_root, "bin", "receiver")
    if not os.path.exists(bin_sender):
        bin_sender = os.path.join(project_root, "bin", "sender")

    os.makedirs(recv_dir, exist_ok=True)
    os.makedirs(os.path.dirname(os.path.join(project_root, args.output_csv)), exist_ok=True)

    env = os.environ.copy()
    env["PATH"] = r"C:\mingw64\bin;" + env.get("PATH", "")

    all_files = sorted(os.listdir(real_dir))
    if args.limit > 0 and args.limit < len(all_files):
        # Pick an inclusive representative subset containing both images and videos
        videos = [f for f in all_files if f.endswith(".mp4")]
        images = [f for f in all_files if f.endswith(".jpg")]
        num_vids = min(len(videos), max(2, args.limit // 10))
        num_imgs = args.limit - num_vids
        selected_files = sorted(images[:num_imgs] + videos[:num_vids])
    else:
        selected_files = all_files

    print("=" * 140)
    print(f" BMTP DATASET TRANSMISSION ENGINE: Transferring {len(selected_files)} files from Part A Dataset")
    print(f" Chunk Size: {args.chunk_size} bytes | Window Size: {args.window} | Base Port: {args.base_port}")
    print("=" * 140)

    hdr_fmt = "{:<4} | {:<24} | {:>10} | {:>8} | {:>10} | {:<32} | {:<32} | {:<6}"
    row_fmt = "{:4d} | {:<24} | {:10d} | {:8.3f} | {:10.2f} | {:<32} | {:<32} | {:<6}"

    print(hdr_fmt.format("No.", "Filename", "Size (B)", "Time (s)", "Rate (Mbps)", "Original SHA-256 (pref)", "Received SHA-256 (pref)", "Status"))
    print("-" * 140)

    csv_path = os.path.join(project_root, args.output_csv)
    passed = 0
    failed = 0
    total_bytes = 0
    t_start_total = time.time()

    current_port = args.base_port

    with open(csv_path, "w", encoding="utf-8") as csv_file:
        csv_file.write("index,filename,filesize_bytes,transfer_time_sec,throughput_mbps,orig_sha256,recv_sha256,status\n")

        for idx, fname in enumerate(selected_files, 1):
            src_file = os.path.join(real_dir, fname)
            dst_file = os.path.join(recv_dir, fname)
            if os.path.exists(dst_file):
                try:
                    os.remove(dst_file)
                except Exception:
                    pass

            file_size = os.path.getsize(src_file)
            total_bytes += file_size

            # Assign fresh port for each transfer to avoid TIME_WAIT collision
            current_port += 1
            if current_port > 60000:
                current_port = args.base_port

            # Spawn receiver
            p_recv = subprocess.Popen([
                bin_receiver, "-p", str(current_port), "-o", recv_dir, "-q", "-1"
            ], env=env)
            time.sleep(0.04)

            t0 = time.time()
            res = subprocess.run([
                bin_sender, "-f", src_file, "-p", str(current_port),
                "-c", str(args.chunk_size), "-w", str(args.window), "-q"
            ], env=env, capture_output=True)
            t_file = time.time() - t0

            # Wait for receiver process
            try:
                p_recv.wait(timeout=15)
            except subprocess.TimeoutExpired:
                p_recv.kill()

            # Verify hashes
            with open(src_file, "rb") as fp:
                orig_hash = hashlib.sha256(fp.read()).hexdigest()

            if os.path.exists(dst_file):
                with open(dst_file, "rb") as fp:
                    recv_hash = hashlib.sha256(fp.read()).hexdigest()
            else:
                recv_hash = "FILE_NOT_FOUND"

            match = (orig_hash == recv_hash)
            status = "PASS" if match else "FAIL"
            if match:
                passed += 1
            else:
                failed += 1

            mbps = (file_size * 8 / (t_file * 1e6)) if t_file > 0 else 0.0

            # Print every result (or every 10th if batch is huge)
            if len(selected_files) <= 100 or idx <= 10 or idx % 20 == 0 or idx == len(selected_files) or not match:
                print(row_fmt.format(idx, fname[:24], file_size, t_file, mbps, orig_hash[:32], recv_hash[:32], status), flush=True)

            csv_file.write(f"{idx},{fname},{file_size},{t_file:.6f},{mbps:.4f},{orig_hash},{recv_hash},{status}\n")
            csv_file.flush()

    t_end_total = time.time()
    total_duration = t_end_total - t_start_total
    avg_throughput = (total_bytes * 8 / (total_duration * 1e6)) if total_duration > 0 else 0

    print("=" * 140)
    print(f" Summary: {passed}/{len(selected_files)} Verified Bit-for-Bit Identical | Failures: {failed}")
    print(f" Total Volume Transferred: {total_bytes / (1024*1024):.2f} MB across {len(selected_files)} files")
    print(f" Total Wall Clock Time: {total_duration:.2f} s | Aggregate Goodput: {avg_throughput:.2f} Mbps")
    print(f" Full Verification Log & Hashes Saved: {args.output_csv}")
    print("=" * 140)

    if failed > 0:
        sys.exit(1)

if __name__ == "__main__":
    main()
