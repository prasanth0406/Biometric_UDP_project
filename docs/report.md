# Technical Report: Biometric Data Transfer over TCP
**Course**: Computer Networks  
**Topic Area**: Transport Layer (TCP)  
**Author / Engineering Role**: Senior Network Engineer & Systems Developer  
**Implementation Language**: C (C11, POSIX/BSD Sockets)  
**Target Environment**: Linux / WSL2 (Ubuntu 26.04 LTS x86_64)

---

## 1. Executive Summary & Design Rationale
Transporting sensitive biometric templates (such as fingerprint minutiae vectors, iris scans, and facial feature matrices) requires two fundamental network properties: **uncompromised byte-level integrity** and **robust, deterministic transport delivery**. 

While the Transmission Control Protocol (TCP, `SOCK_STREAM`) provides an abstraction of a reliable byte stream, it introduces key challenges for application engineers:
1. **Lack of Message Boundaries**: TCP treats application data as an unformatted sequence of octets. PDU (Protocol Data Unit) delineation requires application-layer framing.
2. **End-to-End Integrity Verification**: In accordance with the classic End-to-End Argument (Saltzer et al., 1984), transport-layer checksums alone (16-bit one's complement) are insufficient to guarantee that application memory was not corrupted by hardware faults, middlebox tampering, or buffer overruns.

To solve these challenges, we designed and implemented the **Biometric Transport Protocol (BMTP)** in pure C11. BMTP implements an explicit 18-byte binary framing protocol, per-chunk IEEE 802.3 CRC32 verification, full-file FIPS 180-2 SHA-256 cryptographic verification, Automatic Repeat reQuest (ARQ) with Stop-and-Wait and Sliding Window modes, RFC 6298 adaptive retransmission timeout (RTO), and comprehensive Wireshark-style tabular event logging.

---

## 2. Protocol Architecture & Application-Layer Framing

### 2.1 Frame Header Specification
Every transmission unit in BMTP begins with a fixed, packed 18-byte binary header serialized strictly in network byte order (Big-Endian). Explicit field serialization prevents compiler padding and alignment anomalies across heterogeneous architectures:

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|       'B'     |      'M'      |      'T'      |      'P'      | (Magic: 0x424D5450)
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|    Version    |  Message Type |        Sequence Number        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|      (Seq Num continued)      |         Payload Length        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|    (Payload Len continued)    |      Payload CRC32 Checksum   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|      (CRC32 continued)        | Payload Data (0 to N bytes)...|
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Field Name | Size | Type / Encoding | Description |
| :--- | :--- | :--- | :--- |
| **Magic Bytes** | 4 Bytes | ASCII `BMTP` | Identifies valid protocol frames; corrupt streams are instantly rejected |
| **Version** | 1 Byte | `uint8_t` | Protocol version (`0x01`) |
| **Message Type** | 1 Byte | `enum uint8_t` | `1:META`, `2:DATA`, `3:ACK`, `4:NACK`, `5:END`, `6:ERROR` |
| **Sequence Number**| 4 Bytes | `uint32_t` (Big-Endian) | Chunk sequence index ($0 \le seq < total\_chunks$) |
| **Payload Length** | 4 Bytes | `uint32_t` (Big-Endian) | Exact payload byte count (bounded allocation prevents DoS) |
| **Payload CRC32** | 4 Bytes | `uint32_t` (Big-Endian) | Polynomial `0xEDB88320` checksum of payload |

### 2.2 Protocol Exchange Workflow
1. **Connection & Handshake**: The sender initiates `connect()`. Handshake latency is measured with `clock_gettime(CLOCK_MONOTONIC)`. `TCP_NODELAY` is enabled on both endpoints to disable Nagle's algorithm.
2. **Metadata Exchange (META)**: The sender transmits `META` containing filename, file size, chunk size, total chunk count, window size, and the 256-bit SHA-256 digest of the entire file. The receiver validates and responds with `ACK 0`.
3. **Chunked Data Transmission (DATA)**:
   - **Stop-and-Wait ($W=1$)**: Sender transmits chunk $i$, awaits `ACK i` with adaptive RTO via `poll()`. If RTO expires or `NACK i` is received, sender retransmits.
   - **Sliding Window ($W > 1$)**: Sender maintains an active window $[base, base + W)$, pipelining frames and tracking per-frame timeouts.
4. **Integrity Check & Teardown (END)**: After all chunks are ACKed, sender transmits `END`. The receiver reassembles the file, computes its SHA-256 digest, compares it with the expected hash, and emits `[INTEGRITY_PASS]` or `[INTEGRITY_FAIL]`. Connection teardown follows via `close()` (`FIN`).

---

## 3. Demonstration of Core Transport Layer Features

| TCP Feature | Implementation & Observation Mechanism |
| :--- | :--- |
| **3-Way Handshake** | `sender.c` measures elapsed time across `connect()` syscall; `receiver.c` records `accept()` timestamp. Kernel TCP handshake latency logged in microseconds (mean: 0.11 ms). |
| **Sequence & ACK Tracking** | Every DATA chunk carries a 32-bit sequence number. ACKs and NACKs mirror the chunk index, logged with per-frame RTT. |
| **Flow Control** | Receiver buffers dynamically received chunks in memory and monitors kernel `SO_RCVBUF` (131,072 bytes default). |
| **Congestion Control** | On Linux, kernel metrics are queried via `getsockopt(fd, IPPROTO_TCP, TCP_INFO)` to extract `tcpi_rtt`, `tcpi_rto`, `tcpi_snd_cwnd`, and `tcpi_total_retrans`. |
| **Retransmission & RTO** | Implements RFC 6298 adaptive timers ($SRTT$, $RTTVAR$, $RTO = SRTT + 4 \times RTTVAR$) with exponential backoff on timeout. Fault injection (`--corrupt-prob`) triggers CRC failure and Fast Retransmission via NACK. |
| **Connection Teardown** | Clean two-way teardown via `close()` ensuring graceful FIN exchange without RST resets. |

---

## 4. Experimental Evaluation & Benchmark Results

Benchmarking was executed on a Linux/WSL2 system with a 65,536-byte (64 KB) opaque biometric minutiae template. Each experimental configuration was repeated **5 times**, reporting mean and sample standard deviation ($\sigma$).

### 4.1 Summary of Experimental Data (`results/results.csv`)

| Experiment | Tested Variable | Transfer Time (s) | Goodput (KB/s) | Retransmissions | Effective Loss % | Overhead % | Average RTT (ms) |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Exp A: Chunk Size** | 512 Bytes | $0.4861 \pm 0.100$ | $135.99 \pm 26.5$ | $0.00 \pm 0.0$ | $0.00\%$ | $3.89\%$ | $1.79 \pm 0.43$ |
| | 1024 Bytes | $0.3341 \pm 0.060$ | $196.21 \pm 32.6$ | $0.00 \pm 0.0$ | $0.00\%$ | $2.24\%$ | $2.21 \pm 0.29$ |
| | 4096 Bytes | $0.1492 \pm 0.045$ | $456.32 \pm 115.5$| $0.00 \pm 0.0$ | $0.00\%$ | $0.96\%$ | $2.89 \pm 1.16$ |
| | 8192 Bytes | $0.0955 \pm 0.018$ | $686.66 \pm 111.7$| $0.00 \pm 0.0$ | $0.00\%$ | $0.75\%$ | $2.32 \pm 0.52$ |
| **Exp B: Loss Rate** | 0% Loss | $0.3645 \pm 0.054$ | $179.16 \pm 30.5$ | $0.00 \pm 0.0$ | $0.00\%$ | $2.24\%$ | $2.53 \pm 0.50$ |
| | 1% Loss | $0.3996 \pm 0.249$ | $193.10 \pm 66.6$ | $0.20 \pm 0.4$ | $0.31\%$ | $2.54\%$ | $2.04 \pm 0.22$ |
| | 5% Loss | $2.5001 \pm 1.606$ | $36.92 \pm 25.8$  | $4.20 \pm 3.1$ | $6.01\%$ | $8.08\%$ | $2.60 \pm 0.59$ |
| | 10% Loss | $4.8851 \pm 1.257$ | $13.94 \pm 4.1$   | $8.20 \pm 2.3$ | $11.29\%$ | $13.22\%$ | $2.91 \pm 0.35$ |
| **Exp C: Network Delay**| 0 ms Delay | $0.2935 \pm 0.020$ | $218.86 \pm 14.8$ | $0.00 \pm 0.0$ | $0.00\%$ | $2.24\%$ | $1.95 \pm 0.19$ |
| | 50 ms Delay | $4.1355 \pm 0.041$ | $15.48 \pm 0.15$  | $0.00 \pm 0.0$ | $0.00\%$ | $2.24\%$ | $56.40 \pm 0.23$ |
| | 100 ms Delay | $7.3934 \pm 0.134$ | $8.66 \pm 0.16$   | $0.00 \pm 0.0$ | $0.00\%$ | $2.24\%$ | $106.46 \pm 0.83$|
| **Exp D: Window Size** | Stop-and-Wait ($N=1$)| $1.1864 \pm 0.047$ | $54.01 \pm 2.19$ | $0.00 \pm 0.0$ | $0.00\%$ | $2.24\%$ | $14.09 \pm 0.49$ |
| | Sliding Window ($N=4$)| $0.7535 \pm 0.030$ | $85.05 \pm 3.48$ | $0.00 \pm 0.0$ | $0.00\%$ | $2.24\%$ | $37.74 \pm 0.30$ |
| | Sliding Window ($N=8$)| $0.7635 \pm 0.028$ | $83.92 \pm 3.11$ | $0.00 \pm 0.0$ | $0.00\%$ | $2.24\%$ | $75.62 \pm 0.50$ |
| | Sliding Window ($N=16$)| $0.7505 \pm 0.015$ | $85.30 \pm 1.66$ | $0.00 \pm 0.0$ | $0.00\%$ | $2.24\%$ | $142.69 \pm 1.42$|

---

## 5. Performance Insights & Analysis

1. **Chunk Size vs. Overhead**: As chunk size increases from 512 bytes to 8192 bytes, goodput increases by **405%** (from 135.99 KB/s to 686.66 KB/s). Protocol overhead drops from **3.89%** down to **0.75%**, demonstrating that minimizing framing overhead and system call context switches significantly enhances network efficiency.
2. **Impact of Packet Loss**: Injected packet loss severely impacts Stop-and-Wait throughput due to timeout penalties. While 0% loss achieves 179.16 KB/s, 10% loss causes throughput to drop to 13.94 KB/s (a 92% degradation) due to retransmission latency and exponential RTO backoff.
3. **Latency and Delay Bandwidth Product**: Under 10ms network delay, the Sliding Window ($N=4$) achieved **85.05 KB/s**, representing a **57% throughput increase** over Stop-and-Wait (54.01 KB/s), validating pipelined transmission theory.
4. **Memory Safety & Robustness**: All code was verified using AddressSanitizer and UndefinedBehaviorSanitizer (`-fsanitize=address,undefined`). Reconstructed biometric templates consistently matched the source file's SHA-256 hash (`b1a3d8e806e050f40462000a618dea4859271293a277731ac93f1b86cd327c60`) with **zero bit errors** across all recovery scenarios.

---

## 6. Conclusion
The BMTP project demonstrates that combining standard C11 POSIX sockets with robust application-layer framing, dual checksum integrity verification (CRC32 and SHA-256), and adaptive ARQ provides high performance, reliable biometric file transfer across lossy, delayed network topologies.
