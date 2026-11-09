/* log.h - Wireshark-Style Tabular and CSV Logger */
#ifndef BMTP_LOG_H
#define BMTP_LOG_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    char role[32];
    double start_time;
    uint32_t packet_no;
    FILE *log_fp;
    FILE *csv_fp;
    int quiet;
} bmtp_logger_t;

/**
 * @brief Initialize logger, opening console, text log, and CSV files in log_dir.
 */
int logger_init(bmtp_logger_t *logger, const char *role, const char *log_dir, int quiet);

/**
 * @brief Log a packet-like event (console + log file + CSV file).
 */
void logger_log_packet(bmtp_logger_t *logger, const char *src, const char *dst,
                       const char *protocol, size_t length, const char *info_fmt, ...);

/**
 * @brief Log a connection lifecycle event (TCP handshake, socket options, FIN).
 */
void logger_log_lifecycle(bmtp_logger_t *logger, const char *info_fmt, ...);

/**
 * @brief Close logger files.
 */
void logger_close(bmtp_logger_t *logger);

#endif /* BMTP_LOG_H */
