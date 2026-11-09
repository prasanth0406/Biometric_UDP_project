/* sha256.c - FIPS 180-2 SHA-256 Implementation in C11 */
#include "sha256.h"
#include <stdio.h>
#include <string.h>

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

static const uint32_t K[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

static void sha256_transform(sha256_ctx_t *ctx, const uint8_t data[64]) {
    uint32_t a, b, c, d, e, f, g, h, t1, t2, m[64];

    for (int i = 0, j = 0; i < 16; ++i, j += 4) {
        m[i] = ((uint32_t)data[j] << 24) |
               ((uint32_t)data[j + 1] << 16) |
               ((uint32_t)data[j + 2] << 8) |
               ((uint32_t)data[j + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (int i = 0; i < 64; ++i) {
        t1 = h + EP1(e) + CH(e, f, g) + K[i] + m[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void sha256_init(sha256_ctx_t *ctx) {
    ctx->count = 0;
    ctx->state[0] = 0x6a09e667U;
    ctx->state[1] = 0xbb67ae85U;
    ctx->state[2] = 0x3c6ef372U;
    ctx->state[3] = 0xa54ff53aU;
    ctx->state[4] = 0x510e527fU;
    ctx->state[5] = 0x9b05688cU;
    ctx->state[6] = 0x1f83d9abU;
    ctx->state[7] = 0x5be0cd19U;
}

void sha256_update(sha256_ctx_t *ctx, const void *data, size_t length) {
    const uint8_t *p = (const uint8_t *)data;
    size_t buffer_index = (size_t)(ctx->count & 0x3F);
    ctx->count += length;

    size_t space_left = SHA256_BLOCK_SIZE - buffer_index;
    if (length >= space_left) {
        memcpy(&ctx->buffer[buffer_index], p, space_left);
        sha256_transform(ctx, ctx->buffer);
        p += space_left;
        length -= space_left;
        while (length >= SHA256_BLOCK_SIZE) {
            sha256_transform(ctx, p);
            p += SHA256_BLOCK_SIZE;
            length -= SHA256_BLOCK_SIZE;
        }
        buffer_index = 0;
    }
    if (length > 0) {
        memcpy(&ctx->buffer[buffer_index], p, length);
    }
}

void sha256_final(sha256_ctx_t *ctx, uint8_t digest[SHA256_DIGEST_SIZE]) {
    uint8_t count_bytes[8];
    uint64_t bit_count = ctx->count * 8;

    for (int i = 0; i < 8; ++i) {
        count_bytes[7 - i] = (uint8_t)(bit_count >> (i * 8));
    }

    size_t buffer_index = (size_t)(ctx->count & 0x3F);
    ctx->buffer[buffer_index++] = 0x80;

    if (buffer_index > 56) {
        memset(&ctx->buffer[buffer_index], 0, SHA256_BLOCK_SIZE - buffer_index);
        sha256_transform(ctx, ctx->buffer);
        buffer_index = 0;
    }
    memset(&ctx->buffer[buffer_index], 0, 56 - buffer_index);
    memcpy(&ctx->buffer[56], count_bytes, 8);
    sha256_transform(ctx, ctx->buffer);

    for (int i = 0; i < 8; ++i) {
        digest[i * 4]     = (uint8_t)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}

int sha256_hash_file(const char *filepath, uint8_t digest[SHA256_DIGEST_SIZE]) {
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        return -1;
    }
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    uint8_t buf[16384];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        sha256_update(&ctx, buf, n);
    }
    if (ferror(f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    sha256_final(&ctx, digest);
    return 0;
}

void sha256_to_hex(const uint8_t digest[SHA256_DIGEST_SIZE], char hex_out[65]) {
    static const char hex_digits[] = "0123456789abcdef";
    for (int i = 0; i < SHA256_DIGEST_SIZE; ++i) {
        hex_out[i * 2]     = hex_digits[(digest[i] >> 4) & 0x0F];
        hex_out[i * 2 + 1] = hex_digits[digest[i] & 0x0F];
    }
    hex_out[64] = '\0';
}
