/*
 * rle8.h - RLE for 8-bit elements (format and usage details: rle_common.h)
 */
#ifndef RLE8_H
#define RLE8_H

#include "rle_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RLE8_DEFAULT_INDICATOR  0xA5u
#define RLE8_MAX_RUN            255u

/* Values for rle8_cfg_t.indicator: 0 = default (0xA5u),
 * otherwise value directly or RLE8_IND(v) (required for v = 0) */
#define RLE8_IND(v)   ((uint16_t)(0x100u | ((v) & 0xFFu)))

/* Worst-case size of encoded data in elements */
#define RLE8_BOUND(n) (2u * (n))

typedef int (*rle8_in_fn) (void *user, uint8_t *s);  /* RLE_IN_DATA/WAIT/EOF      */
typedef int (*rle8_out_fn)(void *user, uint8_t s);   /* 1 accepted, 0 sink full   */

/* Unused fields stay 0 or NULL (designated initializer). */
typedef struct {
    uint16_t    indicator;   /* 0 = default; see RLE8_IND()      */
    uint8_t     min_run;     /* 0 = default (4); 2..255; encoder only */
    rle8_in_fn  in;          /* optional: source                  */
    void       *in_user;
    rle8_out_fn out;         /* optional: sink                    */
    void       *out_user;
} rle8_cfg_t;

struct rle8_ops;   /* internal: direction-specific function table */

/* State - fields are internal, use only via the public functions */
typedef struct {
    const struct rle8_ops *ops;
    rle8_in_fn  in;
    void       *in_user;
    rle8_out_fn out;
    void       *out_user;
    uint8_t    ind, held, cur, rep_v;
    uint8_t    q[3];
    uint8_t    count, pending, rep_left;
    uint8_t     min_run, err, ended, flushed, has_held, q_len, q_pos, dstate;
} rle8_t;

/* ---- Streaming ---- */
rle_status_t rle8_enc_init(rle8_t *r, const rle8_cfg_t *cfg);   /* cfg may be NULL */
rle_status_t rle8_dec_init(rle8_t *r, const rle8_cfg_t *cfg);   /* cfg may be NULL */
size_t       rle8_write(rle8_t *r, const uint8_t *buf, size_t n);  /* returns number accepted */
size_t       rle8_read (rle8_t *r, uint8_t *buf, size_t cap);      /* returns number produced */
rle_status_t rle8_run  (rle8_t *r);        /* requires cfg.out */
rle_status_t rle8_end  (rle8_t *r);        /* signal end of input */
rle_status_t rle8_state(rle8_t *r);
rle_err_t    rle8_error(const rle8_t *r);

/* ---- One-shot functions (callbacks in cfg are ignored) ----
 * out == NULL: only determine size (*out_len), cap is ignored.
 * On RLE_E_SPACE, *out_len contains the number of elements written so far. */
size_t    rle8_bound     (size_t n);   /* like RLE8_BOUND, with overflow protection */
rle_err_t rle8_encode_buf(const rle8_cfg_t *cfg, const uint8_t *in, size_t n,
                          uint8_t *out, size_t cap, size_t *out_len);
rle_err_t rle8_decode_buf(const rle8_cfg_t *cfg, const uint8_t *in, size_t n,
                          uint8_t *out, size_t cap, size_t *out_len);

/* ---- Adapter: memory buffer ---- */
typedef struct {
    const uint8_t *rd;
    uint8_t       *wr;
    size_t          len;
    size_t          pos;
} rle8_mem_t;
#define RLE8_MEM_SRC(p, n) { (p), NULL, (n), 0 }
#define RLE8_MEM_DST(p, n) { NULL, (p), (n), 0 }
int rle8_mem_in (void *user, uint8_t *s);    /* DATA or EOF         */
int rle8_mem_out(void *user, uint8_t s);     /* 0 if buffer full    */

/* ---- Adapter: ring buffer (single producer, single consumer) ----
 * Capacity = size - 1. For ISR <-> main-loop on single-core MCUs;
 * on multi-core systems additional memory barriers are required. */
typedef struct {
    uint8_t         *buf;
    size_t            size;
    volatile size_t   head;
    volatile size_t   tail;
    volatile uint8_t  closed;
} rle8_ring_t;
void   rle8_ring_init (rle8_ring_t *q, uint8_t *buf, size_t size);  /* size >= 2 */
int    rle8_ring_put  (rle8_ring_t *q, uint8_t s);                  /* 1 ok, 0 full */
int    rle8_ring_get  (rle8_ring_t *q, uint8_t *s);                 /* 1 ok, 0 empty */
size_t rle8_ring_count(const rle8_ring_t *q);
void   rle8_ring_close(rle8_ring_t *q);   /* producer done -> rle8_ring_in returns EOF */
int    rle8_ring_in   (void *user, uint8_t *s);                      /* DATA/WAIT/EOF */
int    rle8_ring_out  (void *user, uint8_t s);

#ifdef __cplusplus
}
#endif

#endif /* RLE8_H */
