/*
 * rle16.h - RLE for 16-bit elements (format and usage details: rle_common.h)
 */
#ifndef RLE16_H
#define RLE16_H

#include "rle_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RLE16_DEFAULT_INDICATOR  0xCAFEu
#define RLE16_MAX_RUN            65535u

/* Values for rle16_cfg_t.indicator: 0 = default (0xCAFEu),
 * otherwise value directly or RLE16_IND(v) (required for v = 0) */
#define RLE16_IND(v)   ((uint32_t)(0x10000ul | ((v) & 0xFFFFu)))

/* Worst-case size of encoded data in elements */
#define RLE16_BOUND(n) (2u * (n))

typedef int (*rle16_in_fn) (void *user, uint16_t *s);  /* RLE_IN_DATA/WAIT/EOF      */
typedef int (*rle16_out_fn)(void *user, uint16_t s);   /* 1 accepted, 0 sink full   */

/* Unused fields stay 0 or NULL (designated initializer). */
typedef struct {
    uint32_t    indicator;   /* 0 = default; see RLE16_IND()     */
    uint8_t     min_run;     /* 0 = default (4); 2..255; encoder only */
    rle16_in_fn  in;          /* optional: source                 */
    void       *in_user;
    rle16_out_fn out;         /* optional: sink                   */
    void       *out_user;
} rle16_cfg_t;

struct rle16_ops;   /* internal: direction-specific function table */

/* State - fields are internal, use only via the public functions */
typedef struct {
    const struct rle16_ops *ops;
    rle16_in_fn  in;
    void       *in_user;
    rle16_out_fn out;
    void       *out_user;
    uint16_t    ind, held, cur, rep_v;
    uint16_t    q[3];
    uint16_t    count, pending, rep_left;
    uint8_t     min_run, err, ended, flushed, has_held, q_len, q_pos, dstate;
} rle16_t;

/* ---- Streaming ---- */
rle_status_t rle16_enc_init(rle16_t *r, const rle16_cfg_t *cfg);   /* cfg may be NULL */
rle_status_t rle16_dec_init(rle16_t *r, const rle16_cfg_t *cfg);   /* cfg may be NULL */
size_t       rle16_write(rle16_t *r, const uint16_t *buf, size_t n);  /* returns number accepted */
size_t       rle16_read (rle16_t *r, uint16_t *buf, size_t cap);      /* returns number produced */
rle_status_t rle16_run  (rle16_t *r);        /* requires cfg.out */
rle_status_t rle16_end  (rle16_t *r);        /* signal end of input */
rle_status_t rle16_state(rle16_t *r);
rle_err_t    rle16_error(const rle16_t *r);

/* ---- One-shot functions (callbacks in cfg are ignored) ----
 * out == NULL: only determine size (*out_len), cap is ignored.
 * On RLE_E_SPACE, *out_len contains the number of elements written so far. */
size_t    rle16_bound     (size_t n);   /* like RLE16_BOUND, with overflow protection */
rle_err_t rle16_encode_buf(const rle16_cfg_t *cfg, const uint16_t *in, size_t n,
                          uint16_t *out, size_t cap, size_t *out_len);
rle_err_t rle16_decode_buf(const rle16_cfg_t *cfg, const uint16_t *in, size_t n,
                          uint16_t *out, size_t cap, size_t *out_len);

/* ---- Adapter: memory buffer ---- */
typedef struct {
    const uint16_t *rd;
    uint16_t       *wr;
    size_t          len;
    size_t          pos;
} rle16_mem_t;
#define RLE16_MEM_SRC(p, n) { (p), NULL, (n), 0 }
#define RLE16_MEM_DST(p, n) { NULL, (p), (n), 0 }
int rle16_mem_in (void *user, uint16_t *s);    /* DATA or EOF         */
int rle16_mem_out(void *user, uint16_t s);     /* 0 if buffer full    */

/* ---- Adapter: ring buffer (single producer, single consumer) ----
 * Capacity = size - 1. For ISR <-> main-loop on single-core MCUs;
 * on multi-core systems additional memory barriers are required. */
typedef struct {
    uint16_t         *buf;
    size_t            size;
    volatile size_t   head;
    volatile size_t   tail;
    volatile uint8_t  closed;
} rle16_ring_t;
void   rle16_ring_init (rle16_ring_t *q, uint16_t *buf, size_t size);  /* size >= 2 */
int    rle16_ring_put  (rle16_ring_t *q, uint16_t s);                  /* 1 ok, 0 full */
int    rle16_ring_get  (rle16_ring_t *q, uint16_t *s);                 /* 1 ok, 0 empty */
size_t rle16_ring_count(const rle16_ring_t *q);
void   rle16_ring_close(rle16_ring_t *q);   /* producer done -> rle16_ring_in returns EOF */
int    rle16_ring_in   (void *user, uint16_t *s);                      /* DATA/WAIT/EOF */
int    rle16_ring_out  (void *user, uint16_t s);

#ifdef __cplusplus
}
#endif

#endif /* RLE16_H */
