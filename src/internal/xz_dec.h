/* First-party XZ container decoder. */

#ifndef QUIREWISE_COMPRESSION_XZ_DEC_H
#define QUIREWISE_COMPRESSION_XZ_DEC_H

#include <stddef.h>
#include <stdint.h>

/* Decode one complete .xz stream. Returns 1 on success. In prefix mode the
 * decoder stops once out_size bytes have been produced (a partial block is
 * allowed) and reports the produced size in *out_written. */
int qw_xz_decode(const uint8_t *in, size_t in_size, uint8_t *out,
                 size_t out_size, size_t memory_limit, int allow_prefix,
                 size_t *out_written);

#endif
