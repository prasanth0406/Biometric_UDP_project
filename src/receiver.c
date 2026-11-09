/* receiver.c - BMTP Biometric Data TCP Receiver Server */
#define _POSIX_C_SOURCE 200809L
#include "crc32.h"
#include "log.h"
#include "net_util.h"
#include "protocol.h"
#include "sha256.h"

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static volatile sig_atomic_t g_running = 1;

static void handle_sigint(int sig) {
    (void)sig;
    g_running = 0;
}

static void ensure_dir(const char *dir) {
#ifdef _WIN32
    mkdir(dir);
#else
    mkdir(dir, 0755);
#endif
}

typedef struct {
    uint8_t *data;
    uint32_t len;
    int received;
} chunk_entry_t;

int main(int argc, char *argv[]) {
    char bind_ip[64] = "0.0.0.0";
    int port = 5001;
    char output_dir[256] = "received_files";
    char log_dir[256] = "logs";
    int quiet = 0;
    int single_transfer = 0; /* Default: 0 = continuous server mode; -1 sets 1 */

    int opt;
    while ((opt = getopt(argc, argv, "b:p:o:l:q1mh")) != -1) {
        switch (opt) {
            case 'b': snprintf(bind_ip, sizeof(bind_ip), "%s", optarg); break;
            case 'p': {
                int p = atoi(optarg);
                if (p <= 0 || p > 65535) {
                    fprintf(stderr, "Error: Invalid port '%s'. Must be between 1 and 65535.\n", optarg);
                    return 1;
                }
                port = p;
                break;
            }
            case 'o': snprintf(output_dir, sizeof(output_dir), "%s", optarg); break;
            case 'l': snprintf(log_dir, sizeof(log_dir), "%s", optarg); break;
            case 'q': quiet = 1; break;
            case '1': single_transfer = 1; break;
            case 'm': single_transfer = 0; break;
            case 'h':
            default:
                fprintf(stderr, "Usage: %s [-b bind_ip] [-p port] [-o output_dir] [-l log_dir] [-q] [-1] [-m]\n", argv[0]);
                return 1;
        }
    }

    signal(SIGINT, handle_sigint);
    ensure_dir(output_dir);
    ensure_dir(log_dir);

    if (net_init() != 0) {
        fprintf(stderr, "Failed to initialize network subsystem.\n");
        return 1;
    }

    bmtp_logger_t logger;
    if (logger_init(&logger, "receiver", log_dir, quiet) != 0) {
        fprintf(stderr, "Failed to initialize logger in %s\n", log_dir);
        net_cleanup();
        return 1;
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        logger_log_lifecycle(&logger, "[ERROR] socket() failed: %s", strerror(errno));
        logger_close(&logger);
        net_cleanup();
        return 1;
    }

    int reuse = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&reuse, sizeof(reuse)) < 0) {
        logger_log_lifecycle(&logger, "[WARN] setsockopt SO_REUSEADDR failed: %s", strerror(errno));
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, bind_ip, &serv_addr.sin_addr) <= 0) {
        serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    if (bind(listen_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        logger_log_lifecycle(&logger, "[FATAL] bind() to %s:%d failed: %s", bind_ip, port, strerror(errno));
        CLOSE_SOCKET(listen_fd);
        logger_close(&logger);
        net_cleanup();
        return 1;
    }

    if (listen(listen_fd, 5) < 0) {
        logger_log_lifecycle(&logger, "[FATAL] listen() failed: %s", strerror(errno));
        CLOSE_SOCKET(listen_fd);
        logger_close(&logger);
        net_cleanup();
        return 1;
    }

    logger_log_lifecycle(&logger, "[LISTEN] Server listening on %s:%d", bind_ip, port);

    int exit_status = 1;

    do {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (!g_running || errno == EINTR) {
                break;
            }
            logger_log_lifecycle(&logger, "[ERROR] accept() failed: %s", strerror(errno));
            break;
        }

        set_tcp_nodelay(client_fd, 1);

        char local_str[64], peer_str[64];
        get_socket_addr_str(client_fd, local_str, sizeof(local_str), 0);
        get_socket_addr_str(client_fd, peer_str, sizeof(peer_str), 1);

        int sndbuf = 0, rcvbuf = 0;
        get_socket_buffers(client_fd, &sndbuf, &rcvbuf);

        logger_log_lifecycle(&logger, "[SYN-ACK] Connection accepted from %s (opts: TCP_NODELAY=1, SO_RCVBUF=%d)",
                             peer_str, rcvbuf);

        bmtp_meta_t meta;
        memset(&meta, 0, sizeof(meta));
        int meta_received = 0;
        chunk_entry_t *chunks = NULL;
        uint32_t chunks_stored = 0;
        uint32_t expected_seq = 0;
        uint32_t nacks_sent = 0;
        uint32_t crc_errors = 0;

        while (1) {
            bmtp_header_t hdr;
            uint8_t *payload = NULL;
            int crc_valid = 1;

            int res = bmtp_recv_frame(client_fd, &hdr, &payload, &crc_valid);
            if (res != 0) {
                if (res == 1) {
                    logger_log_lifecycle(&logger, "[FIN] Peer closed connection");
                } else {
                    logger_log_lifecycle(&logger, "[ERROR] Frame receive failed");
                }
                if (payload) free(payload);
                break;
            }

            if (hdr.msg_type == BMTP_MSG_META) {
                if (bmtp_unpack_meta(payload, hdr.payload_len, &meta) == 0) {
                    meta_received = 1;
                    expected_seq = 0;
                    chunks_stored = 0;
                    char hex_sha[65];
                    sha256_to_hex(meta.sha256, hex_sha);

                    logger_log_packet(&logger, peer_str, local_str, "BMTP",
                                      BMTP_HEADER_LEN + hdr.payload_len,
                                      "[META] File='%s' Size=%lu Chunks=%u ChunkSize=%u Mode=GoBackN_SlidingWin(W=%u) SHA256=%.12s...",
                                      meta.filename, (unsigned long)meta.filesize,
                                      meta.total_chunks, meta.chunk_size,
                                      meta.window_size, hex_sha);

                    chunks = (chunk_entry_t *)calloc(meta.total_chunks, sizeof(chunk_entry_t));

                    /* Send ACK for META */
                    bmtp_send_frame(client_fd, BMTP_MSG_ACK, 0, NULL, 0, 0);
                    logger_log_packet(&logger, local_str, peer_str, "BMTP", BMTP_HEADER_LEN,
                                      "[ACK] Ack=0 (META accepted)");
                } else {
                    logger_log_lifecycle(&logger, "[ERROR] Invalid META payload received");
                    bmtp_send_frame(client_fd, BMTP_MSG_ERROR, 0, NULL, 0, 0);
                }
                free(payload);
            } else if (hdr.msg_type == BMTP_MSG_DATA) {
                uint32_t seq = hdr.seq_num;
                if (!crc_valid) {
                    if (meta_received && chunks && seq >= expected_seq && seq < meta.total_chunks) {
                        crc_errors++;
                        nacks_sent++;
                        logger_log_packet(&logger, peer_str, local_str, "BMTP",
                                          BMTP_HEADER_LEN + hdr.payload_len,
                                          "[DATA_CORRUPT] Seq=%u Len=%u CRC=%08X -> FAULT DETECTED",
                                          seq, hdr.payload_len, hdr.crc32);

                        /* In Go-Back-N: Send NACK for expected sequence number */
                        bmtp_send_frame(client_fd, BMTP_MSG_NACK, expected_seq, NULL, 0, 0);
                        logger_log_packet(&logger, local_str, peer_str, "BMTP", BMTP_HEADER_LEN,
                                          "[NACK] Ack=%u (CRC mismatch, request GBN retransmission)", expected_seq);
                    }
                    free(payload);
                } else {
                    if (meta_received && chunks && seq < meta.total_chunks) {
                        if (seq == expected_seq) {
                            /* In-order packet: accept and store */
                            chunks[seq].data = payload;
                            chunks[seq].len = hdr.payload_len;
                            chunks[seq].received = 1;
                            chunks_stored++;
                            expected_seq++;

                            logger_log_packet(&logger, peer_str, local_str, "BMTP",
                                              BMTP_HEADER_LEN + hdr.payload_len,
                                              "[DATA] Seq=%u Len=%u CRC=%08X (%u/%u chunks in-order)",
                                              seq, hdr.payload_len, hdr.crc32, chunks_stored, meta.total_chunks);

                            /* Send cumulative ACK */
                            bmtp_send_frame(client_fd, BMTP_MSG_ACK, seq, NULL, 0, 0);
                            logger_log_packet(&logger, local_str, peer_str, "BMTP", BMTP_HEADER_LEN,
                                              "[ACK] Ack=%u (GBN cumulative)", seq);
                        } else if (seq < expected_seq) {
                            /* Duplicate packet: already received in-order earlier */
                            free(payload);
                            logger_log_packet(&logger, peer_str, local_str, "BMTP",
                                              BMTP_HEADER_LEN + hdr.payload_len,
                                              "[DATA_DUP] Seq=%u < Expected=%u -> DISCARDED", seq, expected_seq);

                            /* Resend ACK for expected_seq - 1 so sender advances base */
                            bmtp_send_frame(client_fd, BMTP_MSG_ACK, expected_seq - 1, NULL, 0, 0);
                            logger_log_packet(&logger, local_str, peer_str, "BMTP", BMTP_HEADER_LEN,
                                              "[ACK_DUP] Ack=%u (duplicate acknowledged)", expected_seq - 1);
                        } else {
                            /* Out-of-order packet (seq > expected_seq): DISCARD in Go-Back-N ARQ */
                            free(payload);
                            logger_log_packet(&logger, peer_str, local_str, "BMTP",
                                              BMTP_HEADER_LEN + hdr.payload_len,
                                              "[DATA_OUT_OF_ORDER] Seq=%u Expected=%u -> DISCARDED (GBN)",
                                              seq, expected_seq);

                            /* Send duplicate ACK for last in-order chunk, or NACK if no chunks received yet */
                            if (expected_seq > 0) {
                                bmtp_send_frame(client_fd, BMTP_MSG_ACK, expected_seq - 1, NULL, 0, 0);
                                logger_log_packet(&logger, local_str, peer_str, "BMTP", BMTP_HEADER_LEN,
                                                  "[ACK_DUP] Ack=%u (expected Seq=%u)", expected_seq - 1, expected_seq);
                            } else {
                                bmtp_send_frame(client_fd, BMTP_MSG_NACK, 0, NULL, 0, 0);
                                logger_log_packet(&logger, local_str, peer_str, "BMTP", BMTP_HEADER_LEN,
                                                  "[NACK] Ack=0 (expected Seq=0, got Seq=%u)", seq);
                            }
                        }
                    } else {
                        free(payload);
                    }
                }
            } else if (hdr.msg_type == BMTP_MSG_END) {
                logger_log_packet(&logger, peer_str, local_str, "BMTP", BMTP_HEADER_LEN,
                                  "[END] Seq=%u Transfer complete signal. Reconstructing...", hdr.seq_num);

                int missing = 0;
                for (uint32_t i = 0; i < meta.total_chunks; ++i) {
                    if (!chunks || !chunks[i].received) {
                        missing++;
                    }
                }

                if (missing == 0) {
                    char out_filepath[512];
                    snprintf(out_filepath, sizeof(out_filepath), "%s/%s", output_dir, meta.filename);

                    FILE *fout = fopen(out_filepath, "wb");
                    if (!fout) {
                        logger_log_lifecycle(&logger, "[ERROR] Cannot open destination file '%s': %s", out_filepath, strerror(errno));
                        bmtp_send_frame(client_fd, BMTP_MSG_ERROR, hdr.seq_num, NULL, 0, 0);
                    } else {
                        for (uint32_t i = 0; i < meta.total_chunks; ++i) {
                            fwrite(chunks[i].data, 1, chunks[i].len, fout);
                        }
                        fclose(fout);

                        uint8_t actual_sha[32];
                        if (sha256_hash_file(out_filepath, actual_sha) == 0) {
                            char expected_hex[65], actual_hex[65];
                            sha256_to_hex(meta.sha256, expected_hex);
                            sha256_to_hex(actual_sha, actual_hex);

                            if (memcmp(actual_sha, meta.sha256, 32) == 0) {
                                logger_log_lifecycle(&logger, "[INTEGRITY_PASS] File integrity verified! SHA-256: %s", actual_hex);
                                logger_log_lifecycle(&logger, "[SAVED] Reconstructed file saved to '%s'", out_filepath);

                                bmtp_send_frame(client_fd, BMTP_MSG_ACK, hdr.seq_num, NULL, 0, 0);
                                logger_log_packet(&logger, local_str, peer_str, "BMTP", BMTP_HEADER_LEN,
                                                  "[ACK] Ack=%u Final verification PASS", hdr.seq_num);
                                exit_status = 0;
                            } else {
                                logger_log_lifecycle(&logger, "[INTEGRITY_FAIL] Hash mismatch! Expected=%s Actual=%s", expected_hex, actual_hex);
                                bmtp_send_frame(client_fd, BMTP_MSG_ERROR, hdr.seq_num, NULL, 0, 0);
                            }
                        } else {
                            logger_log_lifecycle(&logger, "[ERROR] Failed to compute SHA-256 on '%s'", out_filepath);
                            bmtp_send_frame(client_fd, BMTP_MSG_ERROR, hdr.seq_num, NULL, 0, 0);
                        }
                    }
                } else {
                    logger_log_lifecycle(&logger, "[INTEGRITY_FAIL] Missing %d chunks upon END", missing);
                    bmtp_send_frame(client_fd, BMTP_MSG_ERROR, hdr.seq_num, NULL, 0, 0);
                }
                if (payload) free(payload);
                break;
            } else {
                if (payload) free(payload);
            }
        }

        /* Clean up chunks */
        if (chunks) {
            for (uint32_t i = 0; i < meta.total_chunks; ++i) {
                if (chunks[i].data) {
                    free(chunks[i].data);
                }
            }
            free(chunks);
        }

        CLOSE_SOCKET(client_fd);
        logger_log_lifecycle(&logger, "[FIN] Client session terminated");

    } while (g_running && !single_transfer);

    CLOSE_SOCKET(listen_fd);
    logger_log_lifecycle(&logger, "[SHUTDOWN] Server stopped");
    logger_close(&logger);
    net_cleanup();

    return exit_status;
}
