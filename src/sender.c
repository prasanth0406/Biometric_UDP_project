/* sender.c - BMTP Biometric Data TCP Sender Client */
#define _POSIX_C_SOURCE 200809L
#include "crc32.h"
#include "log.h"
#include "net_util.h"
#include "protocol.h"
#include "sha256.h"

#include <errno.h>
#include <getopt.h>
#include "bmtp_poll.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    uint8_t *data;
    uint32_t len;
    uint32_t crc32;
    int acked;
    int attempts;
    double send_time;
} chunk_desc_t;

static void print_progress(uint32_t current, uint32_t total, double goodput_kbps) {
    int bar_width = 30;
    float progress = (float)current / (float)total;
    int pos = (int)(bar_width * progress);

    printf("\rProgress: [");
    for (int i = 0; i < bar_width; ++i) {
        if (i < pos) printf("=");
        else if (i == pos) printf(">");
        else printf(" ");
    }
    printf("] %3.0f%% (%u/%u chunks) %.1f KB/s", progress * 100.0f, current, total, goodput_kbps);
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    char file_path[256] = "sample_data/real_biometric/20261102_091525.jpg";
    char host[64] = "127.0.0.1";
    int port = 5001;
    uint32_t chunk_size = 1024;
    uint32_t window_size = 10; /* Go-Back-N ARQ default window size 10 */
    int timeout_ms = 500;
    int max_retries = 5;
    float corrupt_prob = 0.0f;
    int drop_seq = -1;
    char log_dir[256] = "logs";
    int quiet = 0;

    int opt;
    while ((opt = getopt(argc, argv, "f:h:p:c:w:t:r:k:d:l:q?")) != -1) {
        switch (opt) {
            case 'f': snprintf(file_path, sizeof(file_path), "%s", optarg); break;
            case 'h': snprintf(host, sizeof(host), "%s", optarg); break;
            case 'p': {
                int p = atoi(optarg);
                if (p <= 0 || p > 65535) {
                    fprintf(stderr, "Error: Invalid port '%s'. Must be between 1 and 65535.\n", optarg);
                    return 1;
                }
                port = p;
                break;
            }
            case 'c': chunk_size = (uint32_t)atoi(optarg); break;
            case 'w': window_size = (uint32_t)atoi(optarg); break;
            case 't': timeout_ms = atoi(optarg); break;
            case 'r': max_retries = atoi(optarg); break;
            case 'k': corrupt_prob = (float)atof(optarg); break;
            case 'd': drop_seq = atoi(optarg); break;
            case 'l': snprintf(log_dir, sizeof(log_dir), "%s", optarg); break;
            case 'q': quiet = 1; break;
            default:
                fprintf(stderr, "Usage: %s -f <file> [-h host] [-p port] [-c chunk_size] [-w window (default: 10)] [-k corrupt_prob] [-d drop_seq] [-q]\n", argv[0]);
                return 1;
        }
    }

    FILE *f = fopen(file_path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open biometric file '%s': %s\n", file_path, strerror(errno));
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0) {
        fprintf(stderr, "Invalid or empty biometric file.\n");
        fclose(f);
        return 1;
    }

    uint64_t total_size = (uint64_t)fsize;
    uint32_t total_chunks = (uint32_t)((total_size + chunk_size - 1) / chunk_size);

    chunk_desc_t *chunks = (chunk_desc_t *)calloc(total_chunks, sizeof(chunk_desc_t));
    if (!chunks) {
        fclose(f);
        return 1;
    }

    crc32_init();

    for (uint32_t i = 0; i < total_chunks; ++i) {
        chunks[i].data = (uint8_t *)malloc(chunk_size);
        size_t n = fread(chunks[i].data, 1, chunk_size, f);
        chunks[i].len = (uint32_t)n;
        chunks[i].crc32 = crc32_calculate(chunks[i].data, chunks[i].len);
        chunks[i].acked = 0;
        chunks[i].attempts = 0;
        chunks[i].send_time = 0.0;
    }
    fclose(f);

    uint8_t file_sha[32];
    sha256_hash_file(file_path, file_sha);
    char hex_sha[65];
    sha256_to_hex(file_sha, hex_sha);

    const char *base_filename = strrchr(file_path, '/');
    if (!base_filename) base_filename = strrchr(file_path, '\\');
    base_filename = base_filename ? base_filename + 1 : file_path;

    if (net_init() != 0) {
        fprintf(stderr, "Network init failed.\n");
        return 1;
    }

    bmtp_logger_t logger;
    if (logger_init(&logger, "sender", log_dir, quiet) != 0) {
        fprintf(stderr, "Failed to initialize logger.\n");
        return 1;
    }

    logger_log_lifecycle(&logger, "[INIT] Target file '%s' (%lu bytes, %u chunks, SHA256=%s)",
                         base_filename, (unsigned long)total_size, total_chunks, hex_sha);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        logger_log_lifecycle(&logger, "[FATAL] socket() creation failed: %s", strerror(errno));
        logger_close(&logger);
        return 1;
    }

    set_tcp_nodelay(sockfd, 1);

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &serv_addr.sin_addr) <= 0) {
        /* Fall back to getaddrinfo for hostnames */
        struct addrinfo hints, *res = NULL;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", port);

        if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
            logger_log_lifecycle(&logger, "[FATAL] Cannot resolve hostname or IP: %s", host);
            CLOSE_SOCKET(sockfd);
            logger_close(&logger);
            return 1;
        }
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)res->ai_addr;
        serv_addr.sin_addr = ipv4->sin_addr;
        freeaddrinfo(res);
    }

    /* Measure TCP 3-way Handshake Latency */
    logger_log_lifecycle(&logger, "[SYN] Initiating TCP connection to %s:%d", host, port);
    double t_syn = get_monotonic_time();

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        logger_log_lifecycle(&logger, "[FATAL] connect() failed: %s", strerror(errno));
        CLOSE_SOCKET(sockfd);
        logger_close(&logger);
        return 1;
    }

    double handshake_ms = (get_monotonic_time() - t_syn) * 1000.0;

    char local_str[64], peer_str[64];
    get_socket_addr_str(sockfd, local_str, sizeof(local_str), 0);
    get_socket_addr_str(sockfd, peer_str, sizeof(peer_str), 1);

    int sndbuf = 0, rcvbuf = 0;
    get_socket_buffers(sockfd, &sndbuf, &rcvbuf);

    logger_log_lifecycle(&logger, "[SYN-ACK] 3-way handshake completed in %.3f ms (NODELAY=1, SO_SNDBUF=%d)",
                         handshake_ms, sndbuf);

    /* Linux TCP_INFO inspection */
    tcp_info_summary_t tcp_info;
    if (get_tcp_info(sockfd, &tcp_info) == 0 && tcp_info.has_info) {
        logger_log_lifecycle(&logger, "[TCP_INFO] Initial Kernel RTT: %.2f ms, RTO: %.2f ms, cwnd: %u",
                             (double)tcp_info.rtt_us / 1000.0, (double)tcp_info.rto_us / 1000.0, tcp_info.snd_cwnd);
    }

    /* Step 1: Send META frame */
    bmtp_meta_t meta;
    memset(&meta, 0, sizeof(meta));
    snprintf(meta.filename, sizeof(meta.filename), "%s", base_filename);
    meta.filesize = total_size;
    meta.chunk_size = chunk_size;
    meta.total_chunks = total_chunks;
    memcpy(meta.sha256, file_sha, 32);
    meta.mode = 1; /* Go-Back-N Sliding Window */
    meta.window_size = window_size;

    uint8_t meta_payload[BMTP_META_PAYLOAD_LEN];
    bmtp_pack_meta(meta_payload, &meta);

    uint64_t total_wire_bytes = 0;
    double transfer_start = get_monotonic_time();

    double t_meta_send = get_monotonic_time();
    if (bmtp_send_frame(sockfd, BMTP_MSG_META, 0, meta_payload, BMTP_META_PAYLOAD_LEN, 0) != 0) {
        logger_log_lifecycle(&logger, "[FATAL] Failed to send META frame");
        CLOSE_SOCKET(sockfd);
        logger_close(&logger);
        return 1;
    }
    total_wire_bytes += BMTP_HEADER_LEN + BMTP_META_PAYLOAD_LEN;

    logger_log_packet(&logger, local_str, peer_str, "BMTP", BMTP_HEADER_LEN + BMTP_META_PAYLOAD_LEN,
                      "[META] File='%s' Size=%lu Chunks=%u ChunkSize=%u Mode=GoBackN_SlidingWin(W=%u) SHA256=%.12s...",
                      base_filename, (unsigned long)total_size, total_chunks, chunk_size,
                      window_size, hex_sha);

    /* Wait for META ACK */
    bmtp_header_t ack_hdr;
    int crc_valid = 1;
    if (bmtp_recv_frame(sockfd, &ack_hdr, NULL, &crc_valid) != 0 || ack_hdr.msg_type != BMTP_MSG_ACK) {
        logger_log_lifecycle(&logger, "[FATAL] Receiver rejected metadata");
        CLOSE_SOCKET(sockfd);
        logger_close(&logger);
        return 1;
    }
    double meta_rtt = (get_monotonic_time() - t_meta_send) * 1000.0;
    logger_log_packet(&logger, peer_str, local_str, "BMTP", BMTP_HEADER_LEN,
                      "[ACK] Ack=0 (META accepted, RTT=%.2f ms)", meta_rtt);

    /* RFC 6298 Adaptive RTO State */
    double min_rto = (double)timeout_ms / 1000.0;
    if (min_rto < 0.2) min_rto = 0.2;
    double srtt = meta_rtt / 1000.0;
    double rttvar = srtt / 2.0;
    double current_rto = srtt + 4.0 * rttvar;
    if (current_rto < min_rto) current_rto = min_rto;
    if (current_rto > 5.0) current_rto = 5.0;

    uint32_t retransmissions = 0;
    double total_rtt_sum = meta_rtt;
    uint32_t rtt_samples_count = 1;

    srand((unsigned int)time(NULL));

    /* Step 2: Data Transfer Loop (Go-Back-N ARQ Sliding Window, default W=10) */
    uint32_t base = 0;
    uint32_t next_seq = 0;
    double timer_start = 0.0;
    int timer_running = 0;
    uint32_t dup_ack_count = 0;
    uint32_t fast_rx_base = (uint32_t)-1;

    while (base < total_chunks) {
        /* 1. Transmit all chunks within sliding window [base .. base + window_size - 1] */
        while (next_seq < base + window_size && next_seq < total_chunks) {
            if (chunks[next_seq].attempts == 0) {
                chunks[next_seq].attempts = 1;
            }
            chunks[next_seq].send_time = get_monotonic_time();
            if (!timer_running) {
                timer_start = chunks[next_seq].send_time;
                timer_running = 1;
            }

            int corrupt_this = 0;
            int drop_this = 0;

            if (corrupt_prob > 0.0f && ((float)rand() / (float)RAND_MAX) < corrupt_prob) {
                corrupt_this = 1;
            }
            if (drop_seq == (int)next_seq && chunks[next_seq].attempts == 1) {
                drop_this = 1;
            }

            if (!drop_this) {
                bmtp_send_frame(sockfd, BMTP_MSG_DATA, next_seq, chunks[next_seq].data, chunks[next_seq].len, corrupt_this);
                total_wire_bytes += BMTP_HEADER_LEN + chunks[next_seq].len;

                if (chunks[next_seq].attempts > 1) {
                    logger_log_packet(&logger, local_str, peer_str, "BMTP",
                                      BMTP_HEADER_LEN + chunks[next_seq].len,
                                      "[RETRANSMIT_GBN] Seq=%u (attempt %d, Window=[%u..%u]) Len=%u CRC=%08X%s",
                                      next_seq, chunks[next_seq].attempts, base,
                                      (base + window_size - 1 < total_chunks ? base + window_size - 1 : total_chunks - 1),
                                      chunks[next_seq].len, chunks[next_seq].crc32,
                                      corrupt_this ? " [CORRUPT_INJECTED]" : "");
                } else {
                    logger_log_packet(&logger, local_str, peer_str, "BMTP",
                                      BMTP_HEADER_LEN + chunks[next_seq].len,
                                      "[DATA] Seq=%u Window=[%u..%u] Len=%u CRC=%08X%s",
                                      next_seq, base,
                                      (base + window_size - 1 < total_chunks ? base + window_size - 1 : total_chunks - 1),
                                      chunks[next_seq].len, chunks[next_seq].crc32,
                                      corrupt_this ? " [CORRUPT_INJECTED]" : "");
                }
            } else {
                logger_log_lifecycle(&logger, "[FAULT_DROP] Dropped initial send of Seq=%u to force GBN timeout", next_seq);
            }
            next_seq++;
        }

        /* 2. Poll for incoming ACKs / NACKs */
        int poll_timeout_ms = (next_seq < base + window_size && next_seq < total_chunks) ? 0 : 5;
        struct pollfd pfd;
        pfd.fd = sockfd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int pret = poll(&pfd, 1, poll_timeout_ms);
        int fast_retransmit = 0;
        while (pret > 0 && (pfd.revents & POLLIN)) {
            bmtp_header_t resp_hdr;
            int r_crc = 1;
            if (bmtp_recv_frame(sockfd, &resp_hdr, NULL, &r_crc) == 0) {
                double now = get_monotonic_time();
                if (resp_hdr.msg_type == BMTP_MSG_ACK) {
                    uint32_t ack_seq = resp_hdr.seq_num;
                    if (ack_seq < total_chunks) {
                        if (ack_seq >= base) {
                            /* Cumulative ACK: all chunks from base up to ack_seq are acknowledged */
                            for (uint32_t s = base; s <= ack_seq; ++s) {
                                chunks[s].acked = 1;
                            }
                            double rtt = (now - chunks[ack_seq].send_time) * 1000.0;
                            total_rtt_sum += rtt;
                            rtt_samples_count++;

                            /* RFC 6298 Adaptive RTO update */
                            double sample_s = rtt / 1000.0;
                            rttvar = 0.75 * rttvar + 0.25 * (srtt > sample_s ? srtt - sample_s : sample_s - srtt);
                            srtt = 0.875 * srtt + 0.125 * sample_s;
                            current_rto = srtt + 4.0 * rttvar;
                            if (current_rto < min_rto) current_rto = min_rto;
                            if (current_rto > 5.0) current_rto = 5.0;

                            base = ack_seq + 1;
                            dup_ack_count = 0;
                            fast_retransmit = 0;
                            fast_rx_base = (uint32_t)-1;

                            logger_log_packet(&logger, peer_str, local_str, "BMTP", BMTP_HEADER_LEN,
                                              "[ACK] Ack=%u Base=%u (RTT=%.2f ms, AdaptiveRTO=%.0f ms)",
                                              ack_seq, base, rtt, current_rto * 1000.0);

                            if (base == next_seq) {
                                timer_running = 0;
                            } else {
                                timer_start = now;
                                timer_running = 1;
                            }
                        } else if (ack_seq + 1 == base) {
                            /* Duplicate ACK for base - 1 */
                            dup_ack_count++;
                            logger_log_packet(&logger, peer_str, local_str, "BMTP", BMTP_HEADER_LEN,
                                              "[ACK_DUP] Ack=%u Count=%u (Base=%u)", ack_seq, dup_ack_count, base);
                            if (dup_ack_count == 3 && fast_rx_base != base) {
                                fast_retransmit = 1;
                            }
                        }
                    }
                } else if (resp_hdr.msg_type == BMTP_MSG_NACK) {
                    uint32_t nack_seq = resp_hdr.seq_num;
                    logger_log_packet(&logger, peer_str, local_str, "BMTP", BMTP_HEADER_LEN,
                                      "[NACK] Ack=%u (Fault detected at receiver)", nack_seq);
                    if (nack_seq > base && nack_seq < total_chunks) {
                        for (uint32_t s = base; s < nack_seq; ++s) {
                            chunks[s].acked = 1;
                        }
                        base = nack_seq;
                        fast_rx_base = (uint32_t)-1;
                    }
                    if (nack_seq == base && fast_rx_base != base) {
                        fast_retransmit = 1;
                    }
                }
            } else {
                /* Connection closed or error */
                logger_log_lifecycle(&logger, "[ERROR] Connection lost while awaiting ACKs");
                CLOSE_SOCKET(sockfd);
                logger_close(&logger);
                return 1;
            }

            pfd.fd = sockfd;
            pfd.events = POLLIN;
            pfd.revents = 0;
            pret = poll(&pfd, 1, 0);
        }

        /* After draining socket ACKs, execute Fast Retransmit if needed */
        if (fast_retransmit && base < total_chunks && fast_rx_base != base) {
            fast_rx_base = base;
            chunks[base].attempts++;
            if (chunks[base].attempts > max_retries + 1) {
                logger_log_lifecycle(&logger, "[FATAL] Max retransmissions exceeded on Seq=%u", base);
                CLOSE_SOCKET(sockfd);
                logger_close(&logger);
                return 1;
            }
            retransmissions++;
            logger_log_lifecycle(&logger, "[FAST_RETRANSMIT_GBN] Resending from Base=%u (Window=[%u..%u], attempt %d)",
                                 base, base,
                                 (base + window_size - 1 < total_chunks ? base + window_size - 1 : total_chunks - 1),
                                 chunks[base].attempts);
            next_seq = base;
            timer_start = get_monotonic_time();
            timer_running = 1;
            dup_ack_count = 0;
            continue;
        }

        /* 3. Check for timeout on oldest unacknowledged chunk (base) */
        double now = get_monotonic_time();
        if (timer_running && (now - timer_start) > current_rto) {
            fast_rx_base = (uint32_t)-1;
            chunks[base].attempts++;
            if (chunks[base].attempts > max_retries + 1) {
                logger_log_lifecycle(&logger, "[FATAL] Max retransmissions exceeded on Seq=%u", base);
                CLOSE_SOCKET(sockfd);
                logger_close(&logger);
                return 1;
            }
            retransmissions++;
            current_rto *= 1.5;
            if (current_rto > 3.0) current_rto = 3.0;

            logger_log_lifecycle(&logger, "[TIMEOUT_GBN] Seq=%u timed out -> Go Back to Base=%u (resending window, new RTO=%.0f ms, attempt %d)",
                                 base, base, current_rto * 1000.0, chunks[base].attempts);

            next_seq = base; /* Go Back to Base */
            timer_start = now;
            timer_running = 1;
            dup_ack_count = 0;
        }

        if (!quiet && (base % 4 == 0 || base == total_chunks)) {
            double elapsed = get_monotonic_time() - transfer_start;
            double cur_kbps = (elapsed > 0) ? ((double)(base * chunk_size) / 1024.0 / elapsed) : 0;
            print_progress(base, total_chunks, cur_kbps);
        }
    }

    if (!quiet) {
        printf("\n");
    }

    /* Step 3: Send END frame */
    bmtp_send_frame(sockfd, BMTP_MSG_END, total_chunks, NULL, 0, 0);
    total_wire_bytes += BMTP_HEADER_LEN;

    logger_log_packet(&logger, local_str, peer_str, "BMTP", BMTP_HEADER_LEN,
                      "[END] Seq=%u All data sent. Awaiting final SHA-256 integrity verification.", total_chunks);

    bmtp_header_t final_hdr;
    int final_crc = 1;
    int transfer_pass = 0;

    while (bmtp_recv_frame(sockfd, &final_hdr, NULL, &final_crc) == 0) {
        if (final_hdr.msg_type == BMTP_MSG_ACK && final_hdr.seq_num == total_chunks) {
            logger_log_packet(&logger, peer_str, local_str, "BMTP", BMTP_HEADER_LEN,
                              "[ACK] Ack=%u Final verification PASS -> File reconstructed and verified bit-for-bit!", total_chunks);
            transfer_pass = 1;
            break;
        } else if (final_hdr.msg_type == BMTP_MSG_ERROR) {
            logger_log_lifecycle(&logger, "[ERROR] Final integrity check reported FAIL by Receiver");
            break;
        } else if (final_hdr.msg_type == BMTP_MSG_ACK && final_hdr.seq_num < total_chunks) {
            /* Lingering in-flight ACK from earlier chunk; ignore and wait for END ACK */
            continue;
        } else {
            break;
        }
    }

    double total_duration = get_monotonic_time() - transfer_start;

    /* Connection Teardown */
    CLOSE_SOCKET(sockfd);
    logger_log_lifecycle(&logger, "[FIN] Connection closed cleanly");
    logger_close(&logger);

    /* Compute Final Performance Evaluation Metrics */
    double goodput_kbps = (total_duration > 0) ? ((double)total_size / 1024.0 / total_duration) : 0;
    double goodput_mbps = (goodput_kbps * 8.0) / 1000.0;
    double throughput_kbps = (total_duration > 0) ? ((double)total_wire_bytes / 1024.0 / total_duration) : 0;
    double throughput_mbps = (throughput_kbps * 8.0) / 1000.0;

    uint64_t overhead_bytes = total_wire_bytes > total_size ? total_wire_bytes - total_size : 0;
    double overhead_pct = total_wire_bytes > 0 ? ((double)overhead_bytes / (double)total_wire_bytes) * 100.0 : 0;

    uint32_t total_pkts = total_chunks + retransmissions;
    double loss_pct = total_pkts > 0 ? ((double)retransmissions / (double)total_pkts) * 100.0 : 0;
    double avg_rtt = rtt_samples_count > 0 ? (total_rtt_sum / rtt_samples_count) : 0;

    if (!quiet) {
        printf("\n========================================================================\n");
        printf(" BMTP PERFORMANCE EVALUATION SUMMARY\n");
        printf("========================================================================\n");
        printf(" Transfer Status:         %s\n", transfer_pass ? "SUCCESS (PASS)" : "FAILED");
        printf(" Biometric File:          %s (%lu bytes)\n", base_filename, (unsigned long)total_size);
        printf(" Total Chunks:            %u (Chunk size: %u bytes)\n", total_chunks, chunk_size);
        printf(" ARQ Mode:                Go-Back-N ARQ (Window N=%u)\n", window_size);
        printf(" Total Duration:          %.4f seconds\n", total_duration);
        printf(" TCP Handshake Latency:   %.3f ms\n", handshake_ms);
        printf(" Average Application RTT: %.3f ms\n", avg_rtt);
        printf(" Retransmissions:         %u (Effective loss: %.2f%%)\n", retransmissions, loss_pct);
        printf(" Total Wire Volume:       %lu bytes (Protocol overhead: %.2f%%)\n", (unsigned long)total_wire_bytes, overhead_pct);
        printf(" Goodput:                 %.2f KB/s (%.4f Mbps)\n", goodput_kbps, goodput_mbps);
        printf(" Raw Throughput:          %.2f KB/s (%.4f Mbps)\n", throughput_kbps, throughput_mbps);
        printf("========================================================================\n");
    }

    /* Print Machine-Readable Summary Line for bench.sh */
    printf("SUMMARY: status=%s file=%s size=%lu chunks=%u chunk_size=%u window=%u duration=%.4f goodput_kbps=%.2f throughput_kbps=%.2f retransmits=%u loss_pct=%.2f overhead_pct=%.2f handshake_ms=%.3f rtt_avg_ms=%.3f\n",
           transfer_pass ? "PASS" : "FAIL", base_filename, (unsigned long)total_size,
           total_chunks, chunk_size, window_size, total_duration,
           goodput_kbps, throughput_kbps, retransmissions, loss_pct,
           overhead_pct, handshake_ms, avg_rtt);

    /* Free resources */
    for (uint32_t i = 0; i < total_chunks; ++i) {
        free(chunks[i].data);
    }
    free(chunks);
    net_cleanup();

    return transfer_pass ? 0 : 1;
}
