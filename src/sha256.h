/* sha256.h - FIPS 180-2 SHA-256 Cryptographic Hash Implementation */
#ifndef BMTP_SHA256_H
#define BMTP_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_BLOCK_SIZE  64
#define SHA256_DIGEST_SIZE 32

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[SHA256_BLOCK_SIZE];
} sha256_ctx_t;

/**
 * @brief Initialize SHA-256 context.
 */
void sha256_init(sha256_ctx_t *ctx);

/**
 * @brief Ingest input buffer into running SHA-256 state.
 */
void sha256_update(sha256_ctx_t *ctx, const void *data, size_t length);

/**
 * @brief Complete SHA-256 computation and output 32-byte binary digest.
 */
void sha256_final(sha256_ctx_t *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);

/**
 * @brief Compute SHA-256 of an entire file on disk.
 * @return 0 on success, -1 on I/O error.
 */
int sha256_hash_file(const char *filepath, uint8_t digest[SHA256_DIGEST_SIZE]);

/**
 * @brief Format 32-byte digest into a 64-character lowercase hex string + null terminator.
 */
void sha256_to_hex(const uint8_t digest[SHA256_DIGEST_SIZE], char hex_out[65]);

#endif /* BMTP_SHA256_H */
