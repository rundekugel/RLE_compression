/*
 * rle_common.h - shared types for rle8 (bytes) and rle16 (16-bit words)
 *
 * ------------------------------------------------------------------------
 * Wire format (element = byte for rle8, 16-bit word for rle16)
 *   <s>          Literal element (s != indicator)
 *   IND 0        Literal of the indicator value itself
 *   IND n v      Value v repeated n times (n = 1..255 or 1..65535)
 * No header: the caller manages the indicator (and length if needed).
 * Default indicator: 0xA5 (rle8), 0xCAFE (rle16).
 *
 * ------------------------------------------------------------------------
 * Usage (identical for rle8_* and rle16_*)
 *   Direction set at init time: rleX_enc_init() / rleX_dec_init()
 *
 *   Source       Sink         Application calls                 Typical use
 *   rleX_write   rleX_read    rleX_write() + rleX_read()       both sides active
 *   rleX_write   cfg.out      rleX_write(); rleX_run()         source is master
 *   cfg.in       rleX_read    rleX_read()                      sink is master
 *   cfg.in       cfg.out      rleX_run()                       main-loop pump
 *
 *   End of input: rleX_end() or RLE_IN_EOF from cfg.in.
 *   Status at any time: rleX_state() -> RLE_OK     output is ready
 *                                       RLE_AGAIN  waiting for input or sink
 *                                       RLE_END    fully completed
 *                                       RLE_ERROR  details: rleX_error()
 *   Backpressure: rleX_write() returns the number of accepted elements,
 *   cfg.out may return 0 (sink full); no data is lost.
 *   All lengths and capacities count elements, not bytes.
 *
 *   If only one width or direction is used, the linker removes the rest
 *   (-ffunction-sections -fdata-sections -Wl,--gc-sections).
 * ------------------------------------------------------------------------
 */
#ifndef RLE_COMMON_H
#define RLE_COMMON_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RLE_DEFAULT_MIN_RUN   4u   /* encode as run instead of literals at this length or above */

/* return values of the input callback */
#define RLE_IN_DATA   1    /* *s holds an element                */
#define RLE_IN_WAIT   0    /* no data right now, try again later */
#define RLE_IN_EOF  (-1)   /* source exhausted                   */

typedef enum { RLE_OK = 0, RLE_AGAIN, RLE_END, RLE_ERROR } rle_status_t;

typedef enum {
    RLE_E_NONE = 0,
    RLE_E_PARAM,        /* invalid configuration or parameter         */
    RLE_E_STATE,        /* call does not match operating mode          */
    RLE_E_TRUNCATED,    /* end of data in the middle of a sequence     */
    RLE_E_SPACE         /* output buffer too small (one-shot functions) */
} rle_err_t;

const char *rle_strerror(rle_err_t e);

#ifdef __cplusplus
}
#endif

#endif /* RLE_COMMON_H */
