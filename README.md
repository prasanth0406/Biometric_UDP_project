# Biometric Data Transfer over TCP (BMTP)

A university-level Computer Networks assignment implementing reliable, high-performance biometric data transfer in pure **C (C11)** over POSIX TCP sockets, featuring custom application-layer framing, Wireshark-style tabular logging, real packet capture, and performance evaluation under network impairments.

---

## 1. Project Overview & Features

- **Language**: Pure C (C11 standard).
- **Network API**: POSIX / BSD Sockets (`socket`, `bind`, `listen`, `accept`, `connect`, `send`, `recv`, `poll`).
- **Cryptographic & Integrity Verification**:
  - **Per-chunk CRC32**: Table-based IEEE 802.3 implementation (`0xEDB88320`).
  - **Full-file SHA-256**: Public-domain FIPS 180-2 implementation verified against NIST test vectors.
- **Application-Layer Framing (BMTP)**:
  - Packed 18-byte big-endian binary header (`BMTP` magic bytes, version, message type, sequence number, payload length, CRC32).
  - Explicit network byte order serialization avoiding struct padding anomalies.
  - Distinct PDU types: `META`, `DATA`, `ACK`, `NACK`, `END`, `ERROR`.
- **ARQ Transmission Modes**:
  - **Stop-and-Wait ($W=1$)**: Per-chunk ACK with RFC 6298 Adaptive Retransmission Timeout (RTO).
  - **Sliding Window ($W=4, 8, 16$)**: Pipelined frame transmission with cumulative/selective ACKs.
- **Network Impairment Testing**:
  - Linux `tc netem` automation (`scripts/netem_setup.sh`, `scripts/netem_teardown.sh`).
  - Standalone C multiplexed lossy proxy (`src/proxy.c`) supporting loss, delay, and fault injection.
- **Wireshark-Style Tabular Logging (`src/log.c`)**:
  - Formatted console output and synchronized timestamped text and CSV logs.
  - Microsecond timestamps via `clock_gettime(CLOCK_MONOTONIC)`.
  - Measures 3-way handshake latency, logs socket tuning (`TCP_NODELAY`, `SO_RCVBUF`, `SO_SNDBUF`), and queries Linux `TCP_INFO`.

---

## 2. Directory Structure

```
Biometric_UDP_Project/
├── Makefile                       # Build system (all, debug, test, bench, clean)
├── README.md                      # Complete system documentation & Wireshark guide
├── .gitignore                     # Git ignore rules for binaries, logs, and sensitive data
├── bin/                           # Compiled executable binaries
│   ├── sender                     # BMTP Client application
│   ├── receiver                   # BMTP Server application
│   ├── proxy                      # Application-layer lossy proxy
│   ├── capture                    # Libpcap-based packet capture utility
│   └── test_runner                # C11 unit test suite
├── src/                           # C source code
│   ├── crc32.h / crc32.c          # Table-driven IEEE 802.3 CRC32
│   ├── sha256.h / sha256.c        # FIPS 180-2 SHA-256 hash algorithm
│   ├── net_util.h / net_util.c    # Socket helpers, partial send/recv, time
│   ├── protocol.h / protocol.c    # BMTP frame packing/unpacking
│   ├── log.h / log.c              # Wireshark-style tabular logger
│   ├── sender.c                   # Client program
│   ├── receiver.c                 # Server program
│   ├── proxy.c                    # Lossy network impairment proxy
│   └── capture.c                  # Optional libpcap packet capture program
├── tests/                         # Unit tests
│   └── test_runner.c              # NIST vectors, framing, chunking tests
├── scripts/                       # Shell automation & plotting scripts
│   ├── test_e2e.sh                # End-to-end localhost verification script
│   ├── capture.sh                 # tcpdump / tshark capture script
│   ├── analyze_pcap.sh            # Automated tshark trace analysis script
│   ├── demo_capture.sh            # Live demonstration capture runner
│   ├── netem_setup.sh             # Linux tc netem impairment setup
│   ├── netem_teardown.sh          # Linux tc netem teardown
│   ├── bench.sh                   # 4-experiment performance benchmarking runner
│   └── plot.gp                    # Gnuplot publication plotting script
├── sample_data/                   # Sample test files
│   └── biometric_template.dat     # 64 KB synthetic biometric minutiae template
├── received_files/                # Directory where receiver stores reconstructed files
├── logs/                          # Text logs, CSV logs, and .pcap captures
├── results/                       # Consolidated results.csv and gnuplot PNG charts
└── docs/                          # Project documentation
    ├── report.md                  # 2-page technical report
    ├── architecture.mmd           # Mermaid system architecture & sequence diagram
    ├── demo_script.md             # 3-minute video presentation script
    └── viva_notes.md              # 15 viva voce questions and answers
```

---

## 3. Compilation & Build Instructions

### Prerequisites
- Compiler: GCC 11+ with C11 support (`gcc`).
- Standard libraries: `libc`, `libpcap-dev`, `gnuplot`.
- Environment: Linux / WSL2 (Ubuntu 22.04+ / 24.04+ / 26.04+).

### Make Targets
```bash
# 1. Clean build directory
make clean

# 2. Build optimized release binaries with strict flags (-O2 -Wall -Wextra -Wpedantic -Werror)
make all

# 3. Build debug binaries with AddressSanitizer and UndefinedBehaviorSanitizer
make debug

# 4. Run C unit tests and end-to-end localhost validation
make test

# 5. Run full 4-experiment performance evaluation benchmarks
make bench
```

---

## 4. How to Run the Applications

### 4.1 Running the Receiver (Server)
```bash
# Listen on port 5001, storing reconstructed files in received_files/
./bin/receiver -p 5001 -o received_files
```
Options:
- `-b <ip>`: Bind address (default: `0.0.0.0`).
- `-p <port>`: Port to listen on (default: `5001`).
- `-o <dir>`: Directory to store reconstructed files (default: `received_files`).
- `-l <dir>`: Directory for log files (default: `logs`).
- `-q`: Quiet mode (mutes console output).
- `-1`: Exit after completing one transfer.

### 4.2 Running the Sender (Client)
```bash
# Normal Stop-and-Wait transfer with 1024-byte chunks
./bin/sender -f sample_data/biometric_template.dat -h 127.0.0.1 -p 5001 -c 1024 -w 1

# Pipelined Sliding Window transfer (Window size N=8, chunk size 4096)
./bin/sender -f sample_data/biometric_template.dat -h 127.0.0.1 -p 5001 -c 4096 -w 8
```
Options:
- `-f <file>`: Path to biometric data file.
- `-h <host>`: Receiver IP address (default: `127.0.0.1`).
- `-p <port>`: Receiver port (default: `5001`).
- `-c <chunk>`: Chunk size in bytes (default: `1024`; tests `512`, `1024`, `4096`, `8192`).
- `-w <win>`: ARQ Window size $N$ (default: `1` for Stop-and-Wait; `4`, `8`, `16` for Sliding Window).
- `-t <ms>`: ACK timeout in milliseconds (default: `500`; dynamically adapted via RFC 6298 RTO).
- `-r <retries>`: Maximum retransmission attempts per chunk (default: `5`).
- `-k <prob>`: Fault injection probability ($0.0 \le prob \le 1.0$) for payload bit-flips.
- `-d <seq>`: Drop specific sequence number once to test timeout recovery.

### 4.3 Running with Fault Injection (Error Detection & Retransmission)
```bash
# In terminal 1:
./bin/receiver -p 5001 -o received_files -1

# In terminal 2 (inject 15% corruption probability):
./bin/sender -f sample_data/biometric_template.dat -p 5001 -c 1024 -w 1 -k 0.15
```
Watch the receiver detect CRC32 errors, send `[NACK]`, and the sender immediately retransmit the uncorrupted chunk until SHA-256 verification succeeds!

### 4.4 Running with the Lossy Impairment Proxy (`proxy.c`)
```bash
# 1. Start Receiver on port 5001:
./bin/receiver -p 5001 -o received_files -1 &

# 2. Start Lossy Proxy on port 5000 targeting port 5001 (5% drop rate, 20ms delay):
./bin/proxy -l 5000 -p 5001 -m 0.05 -t 20 -1 &

# 3. Start Sender connecting through proxy port 5000:
./bin/sender -f sample_data/biometric_template.dat -p 5000 -c 1024 -w 1 -t 800
```

---

## 5. Packet Capture & Wireshark Analysis Guide

### 5.1 Capturing on Loopback (`lo`)
In Linux and WSL2, loopback traffic between localhost endpoints traverses the `lo` virtual interface.
Run the automated capture script:
```bash
bash scripts/capture.sh 5001 lo
```
Or run the all-in-one demo capture and analysis:
```bash
bash scripts/demo_capture.sh
```
This produces a Wireshark-compatible packet trace in `logs/capture_demo.pcap`.

### 5.2 Wireshark Display Filters
Open the `.pcap` trace in Wireshark (`wireshark logs/capture_demo.pcap`):

| Display Filter | Purpose | What to Observe |
| :--- | :--- | :--- |
| `tcp.port == 5001` | Isolates all transfer traffic | Full communication flow between client and server |
| `tcp.flags.syn == 1` | Isolates connection setup | The 3-way handshake packets (`SYN` and `SYN, ACK`) |
| `tcp.flags.fin == 1` | Isolates connection teardown | The graceful FIN teardown sequence |
| `tcp.analysis.retransmission` | Highlights retransmissions | Segments resent due to timeout or packet loss |
| `tcp.analysis.duplicate_ack` | Identifies duplicate ACKs | ACKs signaling missing or out-of-order packets |

### 5.3 Recommended Wireshark Screenshots for Submission
1. **TCP 3-Way Handshake**: Filter `tcp.flags.syn == 1` showing client `SYN`, server `SYN-ACK`, and final client `ACK`.
2. **Metadata & Data Transfer Stream**: Filter `tcp.port == 5001` showing the BMTP `META` payload followed by sequential `DATA` segments.
3. **Application Retransmission / Fault Recovery**: Filter `tcp.port == 5001` showing a corrupted segment, the receiver's `NACK`, and the sender's retransmission.
4. **Graceful Connection Teardown**: Filter `tcp.flags.fin == 1` showing `FIN, ACK` sequence.
5. **TCP Stream Graphs**: In Wireshark menu:
   - **Statistics -> TCP Stream Graphs -> Time-Sequence Graph (Stevens)** (shows linear progress and window scaling).
   - **Statistics -> TCP Stream Graphs -> Round Trip Time Graph** (shows RTT consistency across segments).

---

## 6. Performance Evaluation Experiments

Run the complete automated benchmark suite:
```bash
make bench
```
This script executes 5 repetitions for each configuration, recording results in `results/results.csv` and generating publication-grade PNG charts via Gnuplot:

1. **`results/exp_chunk_size.png`**: Goodput (KB/s) and Header Overhead (%) vs. Chunk Size (512, 1024, 4096, 8192 bytes).
2. **`results/exp_loss_rate.png`**: Retransmission Count and Goodput Degradation vs. Network Loss Rate (0%, 1%, 5%, 10%).
3. **`results/exp_delay.png`**: Total Transfer Time (s) and Application RTT (ms) vs. One-Way Injected Latency (0, 50, 100 ms).
4. **`results/exp_window_size.png`**: Throughput and Transmission Time: Stop-and-Wait ($N=1$) vs. Sliding Window ($N=4, 8, 16$).

---

## 7. Submission Checklist

- [x] Clean compilation under `gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2`.
- [x] Memory safety verified under AddressSanitizer and UndefinedBehaviorSanitizer (`make debug`).
- [x] Unit test runner passes 29/29 tests (`./bin/test_runner`).
- [x] End-to-end localhost script passes with byte-for-byte SHA-256 match (`scripts/test_e2e.sh`).
- [x] Wireshark logs recorded in `logs/` as `.log` and `.csv`.
- [x] Wireshark capture `.pcap` generated and verified (`logs/capture_demo.pcap`).
- [x] Benchmark data saved to `results/results.csv` and Gnuplot PNG charts generated in `results/`.
- [x] Technical report (`docs/report.md`), Architecture diagram (`docs/architecture.mmd`), Demo script (`docs/demo_script.md`), and Viva notes (`docs/viva_notes.md`) ready for presentation.

---

## 8. Limitations & Assumptions

1. **Biometric Data Opacity**: Biometric templates are treated strictly as opaque byte arrays. No assumptions are made regarding file format, encryption, or structure.
2. **Loopback Traffic Offload**: On Linux loopback (`lo`), the operating system may utilize Generic Segmentation Offload (GSO) or Large Receive Offload (LRO), resulting in Wireshark capturing frames larger than the standard Ethernet MTU (1500 bytes). For physical NIC testing across two machines, MTU segmentation occurs normally.
3. **Bandwidth Limitations**: The built-in proxy simulates packet loss and propagation delay. Bandwidth rate limiting on Linux is handled via the provided `scripts/netem_setup.sh` (`tc netem rate 10mbit`).
#   B i o m e t r i c _ U D P _ p r o j e c t  
 