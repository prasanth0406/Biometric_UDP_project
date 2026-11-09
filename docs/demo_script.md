# 3-Minute Video Demonstration Script
## Biometric Data Transfer over TCP with Wireshark Evaluation

### Scene 1: Introduction and Project Architecture (0:00 - 0:40)
- **Visual**: Show project terminal, directory structure, and `sample_data/real_biometric/`.
- **Narration**:
  > *"Hello! This is our Computer Networks assignment project: Biometric Data Transfer over TCP (BMTP protocol) with custom application-layer framing, Wireshark-style tabular logging, and performance evaluation under network impairments.
  > The system is implemented in standard C11 using TCP sockets, table-driven IEEE 802.3 CRC32, and pure C FIPS 180-2 SHA-256 for cryptographic integrity verification.
  > Our real captured biometric dataset (photographic biometric captures) is treated strictly as opaque byte arrays."*
- **Action / Command**:
  ```bash
  make clean && make all
  ./bin/test_runner
  ```
- **Show on screen**: All 29 unit tests passing with zero errors.

---

### Scene 2: Live Localhost Transfer & Wireshark-Style Logging (0:40 - 1:30)
- **Visual**: Split terminal (Left: Receiver, Right: Sender).
- **Narration**:
  > *"Now we demonstrate a live transmission of an authentic biometric capture on localhost. Notice the Wireshark-style tabular output showing packet sequence numbers, microsecond timestamps, source and destination IP/ports, protocol framing, length, and informative flags."*
- **Commands**:
  - Terminal 1 (Receiver):
    ```bash
    ./bin/receiver -p 5001 -o received_files
    ```
  - Terminal 2 (Sender):
    ```bash
    ./bin/sender -f sample_data/real_biometric/20261102_091525.jpg -p 5001 -c 4096 -w 8
    ```
- **Show on screen**:
  - `[SYN]` -> 3-way handshake measurement (e.g. 0.55 ms).
  - `[META]` -> Metadata carrying filename, size (739,837 bytes), chunk size (4096B), and full-file SHA-256 digest (`fb4b80ec...`).
  - Streaming `[DATA]` chunks with sequence numbers and per-chunk CRC32.
  - `[END]` followed by receiver calculating SHA-256 and emitting `[INTEGRITY_PASS]`.
  - Machine-readable summary line with goodput, throughput, and RTT.

---

### Scene 3: Fault Injection, Detection, and ARQ Retransmission (1:30 - 2:15)
- **Visual**: Demonstrating error detection and automatic repeat request.
- **Narration**:
  > *"Although TCP guarantees stream delivery, application-layer framing protects against byte corruption or middlebox errors. Here, we inject artificial bit-flips using `-k 0.15` (15% corruption probability). Watch the receiver detect CRC32 mismatches and send NACKs, prompting immediate retransmission."*
- **Commands**:
  - Terminal 1:
    ```bash
    ./bin/receiver -p 5002 -o received_files -1
    ```
  - Terminal 2:
    ```bash
    ./bin/sender -f sample_data/real_biometric/20261102_091525.jpg -p 5002 -c 1024 -w 1 -k 0.15
    ```
- **Show on screen**:
  - `[DATA_CORRUPT] Seq=X Len=1024 CRC=... -> FAULT DETECTED`
  - `[NACK] Ack=X (CRC mismatch, request retransmission)`
  - `[RETRANSMIT] Seq=X`
  - Reconstructed file SHA-256 verification: PASS!

---

### Scene 4: Impairment Proxy, PCAP Capture & Wireshark Analysis (2:15 - 3:00)
- **Visual**: Batch transmission script, PCAP trace, and automated benchmarking graphs.
- **Narration**:
  > *"We test network impairments using our lossy proxy (`proxy.c`) to simulate packet loss (5%) and latency (20ms).
  > We batch-transfer real biometric files using `scripts/send_all.sh` to confirm SHA-256 integrity side-by-side.
  > We also generate real `.pcap` traces with `scripts/demo_capture.sh` and evaluate performance across chunk sizes, loss rates, delays, and sliding window sizes, outputting publication-quality PNG charts."*
- **Commands**:
  ```bash
  # Batch send real files and verify SHA-256 side-by-side
  bash scripts/send_all.sh 5

  # Run end-to-end test suite
  bash scripts/test_e2e.sh

  # Run live packet capture demo
  bash scripts/demo_capture.sh

  # View generated benchmark graphs
  ls -lh results/*.png
  ```
- **Show on screen**:
  - Side-by-side SHA-256 match table.
  - PCAP trace analysis with packet counts and wire throughput.
  - Evaluation graphs: `results/exp_chunk_size.png`, `results/exp_loss_rate.png`, `results/exp_delay.png`, `results/exp_window_size.png`.
- **Closing**:
  > *"All real biometric files were transmitted with 100% byte-for-byte fidelity over TCP and verified with dual CRC32 and SHA-256 checks. Thank you!"*
