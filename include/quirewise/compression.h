#ifndef QUIREWISE_COMPRESSION_H
#define QUIREWISE_COMPRESSION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QUIREWISE_COMPRESSION_ABI_VERSION 1u

enum { QW_COMPRESSION_7Z_MAX_CODERS = 4u };

enum {
    QW_COMPRESSION_7Z_COPY_METHOD = 0x00u,
    QW_COMPRESSION_7Z_DELTA_METHOD = 0x03u,
    QW_COMPRESSION_7Z_LZMA1_METHOD = 0x030101u,
    QW_COMPRESSION_7Z_LZMA2_METHOD = 0x21u,
    QW_COMPRESSION_7Z_DEFLATE_METHOD = 0x040108u,
    QW_COMPRESSION_7Z_X86_METHOD = 0x03030103u,
    QW_COMPRESSION_7Z_POWERPC_METHOD = 0x03030205u,
    QW_COMPRESSION_7Z_IA64_METHOD = 0x03030401u,
    QW_COMPRESSION_7Z_ARM_METHOD = 0x03030501u,
    QW_COMPRESSION_7Z_ARMTHUMB_METHOD = 0x03030701u,
    QW_COMPRESSION_7Z_SPARC_METHOD = 0x03030805u
};

typedef struct QwCompression7zCoder {
    uint32_t method;
    const uint8_t *properties;
    size_t properties_size;
} QwCompression7zCoder;

typedef enum QwCompressionInflateStatus {
    QW_COMPRESSION_INFLATE_ERROR = 0,
    QW_COMPRESSION_INFLATE_OK = 1,
    QW_COMPRESSION_INFLATE_OUTPUT_FULL = 2
} QwCompressionInflateStatus;

/* Compression and strict decompression functions return 1 on success and 0 on
 * invalid arguments, malformed input, insufficient output capacity, or a
 * checksum mismatch. Bounded inflate functions return QwCompressionInflateStatus
 * and report the bytes written through out_size. Deflate functions write to
 * caller-owned, non-overlapping buffers. */
uint32_t qw_compression_abi_version(void);
uint32_t qw_compression_crc32(const void *data, size_t size);
uint32_t qw_compression_adler32(const void *data, size_t size);

/* Return SIZE_MAX when a stored-stream bound would overflow size_t. */
size_t qw_compression_raw_stored_bound(size_t input_size);
size_t qw_compression_zlib_stored_bound(size_t input_size);

/* Raw RFC 1951 inflate; output_size is the exact expected decompressed size. */
int qw_compression_inflate_raw(const uint8_t *input, size_t input_size,
                               uint8_t *output, size_t output_size);

/* Raw RFC 1951 inflate into a caller-owned buffer. OUTPUT_FULL means the
 * stream is valid so far but requires a larger output buffer; out_size is the
 * number of bytes written. The complete raw stream must consume all input. */
QwCompressionInflateStatus qw_compression_inflate_raw_bounded(
    const uint8_t *input, size_t input_size,
    uint8_t *output, size_t output_capacity, size_t *out_size);

/* RFC 1950 zlib wrapper around RFC 1951 inflate, including Adler-32 checks. */
int qw_compression_inflate_zlib(const uint8_t *input, size_t input_size,
                                uint8_t *output, size_t output_size);

/* RFC 1950 zlib inflate into a caller-owned buffer. OUTPUT_FULL means the
 * stream is valid so far but requires a larger output buffer; out_size is the
 * number of bytes written. Header, stream end, input length, and Adler-32 are
 * checked before returning OK. */
QwCompressionInflateStatus qw_compression_inflate_zlib_bounded(
    const uint8_t *input, size_t input_size,
    uint8_t *output, size_t output_capacity, size_t *out_size);

/* Inflate exactly one RFC 1952 gzip member into a caller-owned buffer. Header
 * options, CRC-32, ISIZE, and absence of trailing bytes/members are checked. */
int qw_compression_inflate_gzip(const uint8_t *input, size_t input_size,
                                uint8_t *output, size_t output_size);

/* Decode a 7z coder chain using raw LZMA filters and Quirewise Deflate.
 * Input and output buffers must not overlap. memory_limit caps decoder-owned
 * allocations. */
int qw_compression_inflate_7z_chain(
    const QwCompression7zCoder *coders, size_t coder_count,
    const uint8_t *input, size_t input_size,
    uint8_t *output, size_t output_size, size_t memory_limit);

/* Decode one complete XZ stream, requiring exact input and output sizes.
 * memory_limit caps decoder-owned allocations. Input and output buffers must
 * not overlap. */
int qw_compression_inflate_xz(const uint8_t *input, size_t input_size,
                              uint8_t *output, size_t output_size,
                              size_t memory_limit);

/* Decode an XZ stream prefix into exactly output_size bytes. The function
 * stops when the output buffer fills, so it may not validate the stream
 * trailer or remaining input. memory_limit caps decoder-owned allocations.
 * Input and output buffers must not overlap. */
int qw_compression_inflate_xz_prefix(const uint8_t *input, size_t input_size,
                                     uint8_t *output, size_t output_size,
                                     size_t memory_limit);

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
