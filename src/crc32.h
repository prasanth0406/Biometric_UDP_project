/* crc32.h - IEEE 802.3 CRC32 Checksum Implementation */
#ifndef BMTP_CRC32_H
#define BMTP_CRC32_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Initialize CRC32 lookup table (thread-safe, called automatically if needed).
 */
void crc32_init(void);

/**
 * @brief Calculate CRC32 checksum for a given memory buffer.
 * @param data Pointer to the buffer.
 * @param length Size of buffer in bytes.
 * @return 32-bit unsigned CRC32 checksum.
 */
uint32_t crc32_calculate(const void *data, size_t length);

/**
 * @brief Update a running CRC32 checksum with new buffer data.
 * @param crc Previous CRC32 value (or 0 for initial call).
 * @param data Pointer to the buffer.
 * @param length Size of buffer in bytes.
 * @return Updated 32-bit unsigned CRC32 checksum.
 */
uint32_t crc32_update(uint32_t crc, const void *data, size_t length);

#endif /* BMTP_CRC32_H */
