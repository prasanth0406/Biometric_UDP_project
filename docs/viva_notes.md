# Viva Voce Examination Notes
## Biometric Data Transfer over TCP - 15 Essential Questions & Answers

### 1. Why is application-layer framing necessary on top of TCP?
**Answer**: TCP is a stream-oriented protocol (`SOCK_STREAM`), meaning it guarantees an ordered, reliable sequence of bytes without message boundary preservation. A single `send()` call of 1024 bytes may arrive at the receiver across multiple `recv()` calls (fragmentation), or multiple small `send()` calls may be coalesced into a single `recv()` call (aggregation). Application-layer framing (e.g. BMTP's fixed 18-byte header with magic bytes and explicit payload length) allows the application to reliably delineate discrete message units.

---

### 2. How does the TCP 3-way handshake work, and how did your program measure its latency?
**Answer**:
1. Client sends `SYN` (synchronize sequence number).
2. Server responds with `SYN-ACK` (synchronize + acknowledge client sequence number).
3. Client replies with `ACK`.
In our C implementation (`sender.c`), we sampled `clock_gettime(CLOCK_MONOTONIC)` immediately before calling `connect()` and immediately after `connect()` returned. Because `connect()` blocks until the 3-way handshake completes, $(t_1 - t_0)$ measures the full round-trip handshake time.

---

### 3. Why did you implement application-level ACK and ARQ on top of TCP when TCP is already reliable?
**Answer**:
1. **End-to-End Argument (Saltzer et al.)**: Transport-level reliability only guarantees that bytes were placed into the remote operating system's kernel buffer. It does not prove that the application process successfully parsed, validated, and stored the sensitive biometric data.
2. **Pedagogical Demonstration**: Implementing Stop-and-Wait and Sliding Window protocols allows explicit observation of sequence tracking, timeouts, duplicate detection, and flow control.
3. **Application Integrity**: The application ACK validates cryptographic integrity (per-chunk CRC32 and full-file SHA-256) rather than just byte delivery.

---

### 4. What is Nagle's algorithm and why did you use `TCP_NODELAY`?
**Answer**: Nagle's algorithm (`RFC 896`) reduces network congestion by buffering small outgoing packets until an entire MSS (Maximum Segment Size) is accumulated or an outstanding ACK is received. In interactive or custom framing protocols with frequent request-response exchanges (e.g. Stop-and-Wait sending 18-byte ACKs), Nagle's algorithm combined with TCP Delayed ACKs causes artificial 40ms–200ms latency delays. Enabling `TCP_NODELAY` disables Nagle's algorithm, forcing immediate segment transmission.

---

### 5. What is the difference between Flow Control and Congestion Control in TCP?
**Answer**:
- **Flow Control**: Point-to-point mechanism preventing the sender from overwhelming the **receiver's** buffer. Managed dynamically by the receiver advertising its available buffer space via the TCP Receive Window (`rwnd`).
- **Congestion Control**: Global mechanism preventing sender(s) from overwhelming the intermediate **network links and routers**. Managed by the sender dynamically adjusting its Congestion Window (`cwnd`) via algorithms like Slow Start, Congestion Avoidance, Fast Retransmit, and Fast Recovery.

---

### 6. How is the Adaptive Retransmission Timeout (RTO) calculated?
**Answer**: We implement the standard algorithm from **RFC 6298**:
1. Upon initial measurement: $SRTT = RTT$, $RTTVAR = RTT / 2$, $RTO = SRTT + 4 \times RTTVAR$.
2. On subsequent RTT samples:
   $$RTTVAR = (1 - \beta) \cdot RTTVAR + \beta \cdot |SRTT - RTT| \quad (\beta = 0.25)$$
   $$SRTT = (1 - \alpha) \cdot SRTT + \alpha \cdot RTT \quad (\alpha = 0.125)$$
   $$RTO = SRTT + 4 \cdot RTTVAR$$
3. When a timer expires without an ACK, exponential backoff is applied ($RTO \leftarrow RTO \times 1.5$).

---

### 7. What is the purpose of the `TIME_WAIT` state during TCP connection teardown?
**Answer**: When an endpoint initiates an active close (`FIN`), after receiving the final `ACK` it remains in `TIME_WAIT` for $2 \times MSL$ (Maximum Segment Lifetime, typically 60 to 120 seconds). This ensures:
1. Reliable delivery of the final `ACK` in case it is lost (allowing the remote peer to retransmit its `FIN`).
2. Lingering duplicate packets from the closed connection expire in the network and are not misdelivered to a new socket reusing the same IP:port tuple.

---

### 8. Why is network byte order (Big-Endian) necessary and how was it handled?
**Answer**: Different CPU architectures store multi-byte integers differently: x86/x64 uses Little-Endian (least significant byte first), while network protocols and some architectures use Big-Endian (most significant byte first). To prevent byte corruption, all header fields (`seq_num`, `payload_len`, `crc32`) and META numerical fields are explicitly converted to network byte order using `htonl()` / `htons()` when packing, and converted back via `ntohl()` / `ntohs()` when unpacking.

---

### 9. How does `recv()` handle partial reads and how did you prevent buffer overflows?
**Answer**: In TCP, `recv(fd, buf, len, 0)` may return any number of bytes between $1$ and $len$. To read an exact frame or payload, we implemented `recv_all(fd, buf, len)` which loops until `remaining == 0` or returns an error/EOF. To prevent buffer overflows and remote memory exhaustion, `bmtp_recv_frame()` enforces a strict upper limit (10 MB maximum allocation) on untrusted header payload lengths before allocating memory.

---

### 10. How does Sliding Window improve throughput compared to Stop-and-Wait?
**Answer**: In Stop-and-Wait ($W=1$), the channel is idle for an entire Round Trip Time ($RTT$) after transmitting each chunk. Channel utilization is:
$$U = \frac{T_{trans}}{T_{trans} + RTT}$$
In Sliding Window with window size $N$, the sender transmits up to $N$ chunks without waiting for an ACK. As long as $N \ge \frac{RTT + T_{trans}}{T_{trans}}$, the pipe remains full and utilization approaches $100\%$, vastly increasing effective goodput over high-latency links.

---

### 11. What is the difference between CRC32 and SHA-256 in your protocol?
**Answer**:
- **CRC32**: A fast, cyclic redundancy checksum computed per chunk ($O(N)$ with table lookup). It detects accidental transmission errors, bit-flips, and framing bursts with minimal CPU overhead.
- **SHA-256**: A cryptographically secure one-way hash function computed over the entire reconstructed biometric file. It provides collision resistance ($2^{128}$ security), ensuring tamper-evident proof that the file reconstructed at the receiver matches the sender's original file.

---

### 12. How did you verify memory safety and leak-free execution?
**Answer**: The C code was compiled and executed under GCC's AddressSanitizer (`-fsanitize=address`) and UndefinedBehaviorSanitizer (`-fsanitize=undefined`). ASan intercepts all `malloc`, `free`, and pointer accesses, verifying that every allocated buffer is freed, no out-of-bounds reads/writes occur, and no memory leaks exist upon program termination.

---

### 13. What is the difference between Goodput and Raw Throughput?
**Answer**:
- **Raw Throughput**: Total number of bits transmitted over the network interface per second, including application headers, payload data, and retransmissions:
  $$\text{Throughput} = \frac{\text{Total Wire Bytes} \times 8}{\text{Elapsed Duration}}$$
- **Goodput**: Useful payload bits delivered to the application per second (excluding header overhead and retransmitted packets):
  $$\text{Goodput} = \frac{\text{File Size} \times 8}{\text{Elapsed Duration}}$$

---

### 14. What occurs when a bit-flip is injected using `--corrupt-prob`?
**Answer**:
1. Sender flips the first byte of the chunk payload while preserving the original CRC32 in the header.
2. Receiver receives the 18-byte header and payload, recomputes CRC32 on the received payload, and finds a mismatch against `header.crc32`.
3. Receiver logs `[DATA_CORRUPT]` and sends a `[NACK]` frame with `seq_num`.
4. Sender receives `[NACK]`, logs `[RETRANSMIT]`, and immediately retransmits the uncorrupted chunk.
5. Receiver accepts the retransmitted chunk, sends `[ACK]`, and full-file SHA-256 verification succeeds.

---

### 15. How does `poll()` multiplex sockets in `proxy.c` without multi-threading?
**Answer**: `poll()` is an I/O event multiplexing system call that monitors an array of `struct pollfd` file descriptors. In `proxy.c`, `poll()` monitors both the client socket (`fds[0]`) and target server socket (`fds[1]`) for `POLLIN` events. When either socket has incoming bytes ready to read without blocking, `poll()` unblocks, allowing a single thread to bi-directionally forward and impair traffic with zero race conditions.
