#ifndef QUIREWISE_COMPRESSION_H
#define QUIREWISE_COMPRESSION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QUIREWISE_COMPRESSION_ABI_VERSION 1u

/*
 * Compression and decompression functions return 1 on success and 0 on
 * invalid arguments, malformed input, insufficient output capacity, or a
 * checksum mismatch. Inflate functions require the exact output size.
 * Deflate functions write to caller-owned, non-overlapping buffers.
 */
uint32_t qw_compression_abi_version(void);
uint32_t qw_compression_crc32(const void *data, size_t size);
uint32_t qw_compression_adler32(const void *data, size_t size);

/* Return SIZE_MAX when a stored-stream bound would overflow size_t. */
size_t qw_compression_raw_stored_bound(size_t input_size);
size_t qw_compression_zlib_stored_bound(size_t input_size);

/* Raw RFC 1951 inflate; output_size is the exact expected decompressed size. */
int qw_compression_inflate_raw(const uint8_t *input, size_t input_size,
                               uint8_t *output, size_t output_size);

/* RFC 1950 zlib wrapper around RFC 1951 inflate, including Adler-32 checks. */
int qw_compression_inflate_zlib(const uint8_t *input, size_t input_size,
                                uint8_t *output, size_t output_size);

/* RFC 1951 stored-block encoder. Empty input is encoded as one empty block. */
int qw_compression_deflate_raw_stored(const uint8_t *input, size_t input_size,
                                      uint8_t *output, size_t output_capacity,
                                      size_t *out_size);

/* RFC 1950 zlib stream using stored blocks and an Adler-32 trailer. */
int qw_compression_deflate_zlib_stored(const uint8_t *input, size_t input_size,
                                       uint8_t *output,
                                       size_t output_capacity,
                                       size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif
