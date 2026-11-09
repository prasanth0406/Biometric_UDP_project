# 3-Minute Video Demonstration Script
## Biometric Data Transfer over TCP with Wireshark Evaluation

### Scene 1: Introduction and Project Architecture (0:00 - 0:40)
- **Visual**: Show project terminal, directory structure, and `sample_data/biometric_template.dat`.
- **Narration**:
  > *"Hello! This is our Computer Networks laboratory project: Biometric Data Transfer over TCP with custom application-layer framing, Wireshark-style tabular logging, and performance evaluation under network impairments.
  > The project is written in standard C11 using POSIX sockets, with table-driven CRC32 and pure C SHA-256 for integrity verification.
  > Biometric data is sensitive, so raw binary content is never logged. We treat the template as an opaque binary file."*
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
  > *"Now we demonstrate a live transmission on localhost. Notice the Wireshark-style tabular output showing packet sequence, microsecond timestamps from CLOCK_MONOTONIC, source and destination IP/ports, protocol, length, and informative flags."*
- **Commands**:
  - Terminal 1 (Receiver):
    ```bash
    ./bin/receiver -p 5001 -o received_files
    ```
  - Terminal 2 (Sender):
    ```bash
    ./bin/sender -f sample_data/biometric_template.dat -p 5001 -c 1024 -w 1
    ```
- **Show on screen**:
  - `[SYN]` -> 3-way handshake measurement (e.g. 0.08 ms).
  - `[META]` -> Metadata carrying filename, size (65536B), chunk size (1024B), and full-file SHA-256.
  - Streaming `[DATA]` chunks with sequence numbers and CRC32.
  - `[END]` followed by receiver calculating SHA-256 and printing `[INTEGRITY_PASS]`.
  - Machine-readable summary line with goodput, throughput, and RTT.

---

### Scene 3: Fault Injection, Detection, and ARQ Retransmission (1:30 - 2:15)
- **Visual**: Demonstrating error detection and automatic repeat request.
- **Narration**:
  > *"TCP guarantees reliable byte delivery, but application-level framing protects against payload tampering or data corruption. Here, we inject a bit-flip fault using `-k 0.15` (15% corruption probability). Watch the receiver detect the CRC32 mismatch and send a NACK, prompting immediate retransmission."*
- **Commands**:
  - Terminal 1:
    ```bash
    ./bin/receiver -p 5002 -o received_files -1
    ```
  - Terminal 2:
    ```bash
    ./bin/sender -f sample_data/biometric_template.dat -p 5002 -c 1024 -w 1 -k 0.15
    ```
- **Show on screen**:
  - `[DATA_CORRUPT] Seq=X Len=1024 CRC=... -> FAULT DETECTED`
  - `[NACK] Ack=X (CRC mismatch, request retransmission)`
  - `[RETRANSMIT] Seq=X`
  - Reconstructed file SHA-256 verification: PASS!

---

### Scene 4: Impairment Proxy, PCAP Capture & Wireshark Analysis (2:15 - 3:00)
- **Visual**: Wireshark capture and automated benchmarking graphs.
- **Narration**:
  > *"We test real-world network impairments using our lossy proxy (`proxy.c`) to inject packet loss and latency. We also capture packets to `.pcap` using tcpdump and analyze them with `analyze_pcap.sh`.
  > Finally, our automated benchmark suite executes 5 runs across chunk sizes, loss rates, delays, and sliding window sizes, outputting results to CSV and generating publication-quality Gnuplot charts."*
- **Commands**:
  ```bash
  # Run end-to-end test suite
  bash scripts/test_e2e.sh

  # View generated benchmark graphs
  ls -lh results/*.png
  ```
- **Show on screen**:
  - Wireshark trace with filters: `tcp.port==5001`, `tcp.analysis.retransmission`.
  - Gnuplot graphs: `results/exp_chunk_size.png`, `results/exp_loss_rate.png`, `results/exp_delay.png`, `results/exp_window_size.png`.
- **Closing**:
  > *"The biometric data was completely preserved, verified bit-for-bit, and all TCP transport layer mechanics were thoroughly benchmarked. Thank you!"*
