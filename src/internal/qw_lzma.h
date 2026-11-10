/* First-party LZMA1/LZMA2 decoder used by the compression package. */

#ifndef QUIREWISE_COMPRESSION_QW_LZMA_H
#define QUIREWISE_COMPRESSION_QW_LZMA_H

#include <stddef.h>
#include <stdint.h>

typedef struct QwLzmaProps {
    int lc;
    int lp;
    int pb;
    uint32_t dict_size;
} QwLzmaProps;

/* Parse the five-byte LZMA1 properties block. */
int qw_lzma_props_parse(const uint8_t props[5], QwLzmaProps *out);

/* Decode a raw LZMA1 stream. The output buffer doubles as the dictionary.
 * *in_used receives the number of compressed bytes consumed. Returns 1 on
 * success, 0 on malformed input or when the end marker / input ends before
 * the expected output size. */
int qw_lzma1_decode(const uint8_t *in, size_t in_size, const QwLzmaProps *props,
                    uint8_t *out, size_t out_size, size_t *in_used);

/* Decode an LZMA2 stream (as used inside XZ blocks and 7z LZMA2 coders).
 * The output buffer doubles as the dictionary. *in_used receives the number
 * of compressed bytes consumed (including the terminating 0x00 control
 * byte). Returns 1 on success. */
int qw_lzma2_decode(const uint8_t *in, size_t in_size, uint8_t props_byte,
                    uint8_t *out, size_t out_size, size_t *in_used,
                    size_t *out_used);

/* Dictionary size implied by an LZMA2 properties byte. */
uint32_t qw_lzma2_dict_size(uint8_t props_byte);

#endif
