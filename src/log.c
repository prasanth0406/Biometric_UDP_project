/* log.c - Wireshark-Style Tabular Packet Logger Implementation */
#define _POSIX_C_SOURCE 200809L
#include "log.h"
#include "net_util.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static void ensure_dir(const char *dir) {
#ifdef _WIN32
    mkdir(dir);
#else
    mkdir(dir, 0755);
#endif
}

int logger_init(bmtp_logger_t *logger, const char *role, const char *log_dir, int quiet) {
    memset(logger, 0, sizeof(*logger));
    snprintf(logger->role, sizeof(logger->role), "%s", role);
    logger->quiet = quiet;
    logger->start_time = get_monotonic_time();
    logger->packet_no = 0;

    ensure_dir(log_dir);

    time_t now = time(NULL);
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif

    char ts[32];
    strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tm_buf);

    char log_path[256], csv_path[256];
    snprintf(log_path, sizeof(log_path), "%s/%s_%s.log", log_dir, role, ts);
    snprintf(csv_path, sizeof(csv_path), "%s/%s_%s.csv", log_dir, role, ts);

    logger->log_fp = fopen(log_path, "w");
    if (!logger->log_fp) {
        return -1;
    }
    logger->csv_fp = fopen(csv_path, "w");
    if (!logger->csv_fp) {
        fclose(logger->log_fp);
        return -1;
    }

    /* Write CSV Header */
    fprintf(logger->csv_fp, "No,Time_s,Source,Destination,Protocol,Length,Info\n");
    fflush(logger->csv_fp);

    /* Text Log Header Banner */
    const char *header_banner =
        "  No. |   Time(s) |                Source |           Destination | Protocol | Length | Info";
    const char *divider =
        "------+-----------+-----------------------+-----------------------+----------+--------+---------------------------------------------------";

    fprintf(logger->log_fp, "--- BMTP Wireshark-Style Log (%s) ---\n%s\n%s\n",
            role, header_banner, divider);
    fflush(logger->log_fp);

    if (!quiet) {
        printf("\n===================================================================================================\n");
        printf(" BMTP WIRESHARK-STYLE LOG - %s\n", role);
        printf("===================================================================================================\n");
        printf("%s\n%s\n", header_banner, divider);
        fflush(stdout);
    }
    return 0;
}

void logger_log_packet(bmtp_logger_t *logger, const char *src, const char *dst,
                       const char *protocol, size_t length, const char *info_fmt, ...) {
    logger->packet_no++;
    double now = get_monotonic_time();
    double rel_time = now - logger->start_time;

    char info_buf[512];
    va_list args;
    va_start(args, info_fmt);
    vsnprintf(info_buf, sizeof(info_buf), info_fmt, args);
    va_end(args);

    /* Console & TXT formatted output */
    char line[1024];
    snprintf(line, sizeof(line), "%5u | %9.6f | %21s | %21s | %8s | %6zu | %s",
             logger->packet_no, rel_time, src, dst, protocol, length, info_buf);

    if (logger->log_fp) {
        fprintf(logger->log_fp, "%s\n", line);
        fflush(logger->log_fp);
    }

    if (logger->csv_fp) {
        /* Escape quotes for CSV info */
        fprintf(logger->csv_fp, "%u,%.6f,\"%s\",\"%s\",\"%s\",%zu,\"%s\"\n",
                logger->packet_no, rel_time, src, dst, protocol, length, info_buf);
        fflush(logger->csv_fp);
    }

    if (!logger->quiet) {
        printf("%s\n", line);
        fflush(stdout);
    }
}

void logger_log_lifecycle(bmtp_logger_t *logger, const char *info_fmt, ...) {
    char src[64], dst[64];
    snprintf(src, sizeof(src), "[%s]", logger->role);
    snprintf(dst, sizeof(dst), "[system]");

    char info_buf[512];
    va_list args;
    va_start(args, info_fmt);
    vsnprintf(info_buf, sizeof(info_buf), info_fmt, args);
    va_end(args);

    logger_log_packet(logger, src, dst, "TCP_STATE", 0, "%s", info_buf);
}

void logger_close(bmtp_logger_t *logger) {
    if (logger->log_fp) {
        fclose(logger->log_fp);
        logger->log_fp = NULL;
    }
    if (logger->csv_fp) {
        fclose(logger->csv_fp);
        logger->csv_fp = NULL;
    }
}
