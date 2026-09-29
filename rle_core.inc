/*
 * rle_core.inc - shared logic for rle8.c and rle16.c
 *
 * Included exactly once per element width. Expected macros:
 *   RLE_(n)      Name prefix, e.g. rle8_##n
 *   SYM_T        Element type (uint8_t / uint16_t)
 *   CNT_T        Type of the repeat counter (same width as SYM_T)
 *   IND_T        Type of cfg.indicator
 *   SYM_MAX      Maximum element value (0xFF / 0xFFFF)
 *   IND_FLAG     Flag bit for explicit indicator value (RLE8_IND / RLE16_IND)
 *   MAX_RUN      Maximum run length
 *   DEFAULT_IND  Default indicator value
 *
 * Structure: encoder and decoder each have their own core functions (enc_* / dec_*),
 * collected in an ops table. The init function sets the pointer;
 * the public functions are shared loops with no mode checks.
 * All helpers are static, so instances do not collide.
 * Invariant: ops->put() is only called when can_accept() holds.
 */
#include <string.h>

#define RLE_T      RLE_(t)
#define RLE_CFG_T  RLE_(cfg_t)
#define RLE_MEM_T  RLE_(mem_t)
#define RLE_RING_T RLE_(ring_t)

struct RLE_(ops) {
    int  (*accept) (const RLE_T *r);            /* can input be accepted?      */
    void (*put)    (RLE_T *r, SYM_T s);         /* process one element         */
    int  (*get)    (RLE_T *r, SYM_T *s);        /* fetch one output element    */
    void (*end)    (RLE_T *r);                  /* end of input                */
    int  (*pending)(RLE_T *r);                  /* output pending?             */
};

enum { D_LIT = 0, D_IND, D_CNT };

static void fail(RLE_T *r, rle_err_t e)
{
    if (!r->err)
        r->err = (uint8_t)e;
}

/* ============================ Encoder ============================ */

static void enc_seq(RLE_T *r, SYM_T a, SYM_T b, SYM_T c, uint8_t n)
{
    r->q[0] = a; r->q[1] = b; r->q[2] = c;
    r->q_len = n;
    r->q_pos = 0;
}

static void enc_emit_run(RLE_T *r)
{
    SYM_T v = r->cur;
    CNT_T n = r->count;

    if (n == 0)
        return;
    r->count = 0;

    if (v == r->ind) {
        if (n == 1)
            enc_seq(r, r->ind, 0, 0, 2);        /* indicator literal */
        else
            enc_seq(r, r->ind, (SYM_T)n, v, 3);
    } else if (n >= r->min_run) {
        enc_seq(r, r->ind, (SYM_T)n, v, 3);
    } else {
        r->rep_v    = v;                        /* short run as literals, */
        r->rep_left = n;                        /* without intermediate buffer */
    }
}

/* flush the last run once input is done and the output sequence is free */
static void enc_settle(RLE_T *r)
{
    if (r->ended && !r->flushed && r->q_len == 0 && r->rep_left == 0) {
        enc_emit_run(r);
        r->flushed = 1;
    }
}

static int enc_accept(const RLE_T *r)
{
    return r->q_len == 0 && r->rep_left == 0;
}

static void enc_put(RLE_T *r, SYM_T s)
{
    if (r->count && (s != r->cur || r->count == MAX_RUN))
        enc_emit_run(r);
    if (r->count == 0)
        r->cur = s;
    r->count++;
}

static int enc_get(RLE_T *r, SYM_T *s)
{
    int pass;

    for (pass = 0; pass < 2; ++pass) {
        if (r->q_pos < r->q_len) {
            *s = r->q[r->q_pos++];
            if (r->q_pos == r->q_len)
                r->q_pos = r->q_len = 0;
            return 1;
        }
        if (r->rep_left) {
            r->rep_left--;
            *s = r->rep_v;
            return 1;
        }
        enc_settle(r);                          /* push out last run if ready */
    }
    return 0;
}

static void enc_end(RLE_T *r)
{
    r->ended = 1;
    enc_settle(r);
}

static int enc_pending(RLE_T *r)
{
    enc_settle(r);
    return r->q_pos < r->q_len || r->rep_left;
}

static const struct RLE_(ops) k_enc_ops = {
    enc_accept, enc_put, enc_get, enc_end, enc_pending
};

/* ============================ Decoder ============================ */

static int dec_accept(const RLE_T *r)
{
    return r->rep_left == 0;
}

static void dec_put(RLE_T *r, SYM_T s)
{
    switch (r->dstate) {
    case D_LIT:
        if (s == r->ind) {
            r->dstate = D_IND;
        } else {
            r->rep_v    = s;
            r->rep_left = 1;
        }
        break;
    case D_IND:
        if (s == 0) {
            r->rep_v    = r->ind;               /* indicator literal */
            r->rep_left = 1;
            r->dstate   = D_LIT;
        } else {
            r->pending = (CNT_T)s;
            r->dstate  = D_CNT;
        }
        break;
    default: /* D_CNT */
        r->rep_v    = s;
        r->rep_left = r->pending;
        r->dstate   = D_LIT;
        break;
    }
}

static int dec_get(RLE_T *r, SYM_T *s)
{
    if (!r->rep_left)
        return 0;
    r->rep_left--;
    *s = r->rep_v;
    return 1;
}

static void dec_end(RLE_T *r)
{
    if (r->dstate != D_LIT) {
        fail(r, RLE_E_TRUNCATED);
        return;
    }
    r->ended = 1;
}

static int dec_pending(RLE_T *r)
{
    return r->rep_left != 0;
}

static const struct RLE_(ops) k_dec_ops = {
    dec_accept, dec_put, dec_get, dec_end, dec_pending
};

/* ===================== shared helper functions ===================== */

static int can_accept(const RLE_T *r)
{
    return !r->err && !r->ended && r->ops->accept(r);
}

static int get_sym(RLE_T *r, SYM_T *s)
{
    if (r->has_held) {                          /* previously rejected by sink */
        r->has_held = 0;
        *s = r->held;
        return 1;
    }
    return r->ops->get(r, s);
}

static void do_end(RLE_T *r)
{
    if (!r->ended && !r->err)
        r->ops->end(r);
}

/* drain to cfg.out; if sink rejects, the element is held for next time */
static int drain_out(RLE_T *r)
{
    SYM_T s;
    while (get_sym(r, &s)) {
        if (!r->out(r->out_user, s)) {
            r->held     = s;
            r->has_held = 1;
            return 0;
        }
    }
    return 1;
}

/* uninitialized (zeroed) state guard */
static int not_ready(RLE_T *r)
{
    if (r->ops)
        return 0;
    fail(r, RLE_E_STATE);
    return 1;
}

/* ============================ Init ============================ */

static void init_base(RLE_T *r, const RLE_CFG_T *cfg, const struct RLE_(ops) *ops)
{
    memset(r, 0, sizeof *r);
    r->ops     = ops;
    r->ind     = DEFAULT_IND;
    r->min_run = RLE_DEFAULT_MIN_RUN;
    if (!cfg)
        return;
    r->in       = cfg->in;
    r->in_user  = cfg->in_user;
    r->out      = cfg->out;
    r->out_user = cfg->out_user;
    if (cfg->indicator > (IND_T)(IND_FLAG | SYM_MAX))
        fail(r, RLE_E_PARAM);
    else if (cfg->indicator != 0)
        r->ind = (SYM_T)(cfg->indicator & SYM_MAX);
}

rle_status_t RLE_(enc_init)(RLE_T *r, const RLE_CFG_T *cfg)
{
    if (!r)
        return RLE_ERROR;
    init_base(r, cfg, &k_enc_ops);
    if (cfg && cfg->min_run) {
        r->min_run = cfg->min_run;
        if (r->min_run < 2)
            fail(r, RLE_E_PARAM);
    }
    return r->err ? RLE_ERROR : RLE_OK;
}

rle_status_t RLE_(dec_init)(RLE_T *r, const RLE_CFG_T *cfg)
{
    if (!r)
        return RLE_ERROR;
    init_base(r, cfg, &k_dec_ops);              /* min_run is irrelevant for the decoder */
    return r->err ? RLE_ERROR : RLE_OK;
}

/* ========================= Streaming API ========================= */

size_t RLE_(write)(RLE_T *r, const SYM_T *buf, size_t n)
{
    size_t i = 0;

    if (not_ready(r) || r->err)
        return 0;
    if (r->in || r->ended) {
        fail(r, RLE_E_STATE);                   /* source is cfg.in or already ended */
        return 0;
    }
    while (i < n) {
        if (r->out && !drain_out(r))
            break;
        if (!can_accept(r))
            break;
        r->ops->put(r, buf[i]);
        ++i;
    }
    if (r->out && !r->err)
        (void)drain_out(r);
    return i;
}

size_t RLE_(read)(RLE_T *r, SYM_T *buf, size_t cap)
{
    size_t o = 0;
    SYM_T  s;
    int    v;

    if (not_ready(r))
        return 0;
    if (r->out) {
        fail(r, RLE_E_STATE);                   /* sink is cfg.out */
        return 0;
    }
    while (o < cap) {
        if (get_sym(r, &s)) {
            buf[o++] = s;
            continue;
        }
        if (!r->in || r->ended || r->err)
            break;
        v = r->in(r->in_user, &s);
        if (v == RLE_IN_DATA)
            r->ops->put(r, s);                  /* nothing pending -> allowed */
        else if (v == RLE_IN_EOF)
            do_end(r);
        else
            break;                              /* RLE_IN_WAIT */
    }
    return o;
}

rle_status_t RLE_(run)(RLE_T *r)
{
    SYM_T s;
    int   v;

    if (not_ready(r))
        return RLE_ERROR;
    if (!r->out) {
        fail(r, RLE_E_STATE);
        return RLE_ERROR;
    }
    for (;;) {
        if (!drain_out(r))
            return r->err ? RLE_ERROR : RLE_AGAIN;   /* sink full */
        if (r->err)
            return RLE_ERROR;
        if (r->ended)
            return RLE_END;
        if (!r->in)
            return RLE_AGAIN;                        /* waiting for write() */
        v = r->in(r->in_user, &s);
        if (v == RLE_IN_DATA)
            r->ops->put(r, s);
        else if (v == RLE_IN_EOF)
            do_end(r);
        else
            return RLE_AGAIN;                        /* source empty */
    }
}

rle_status_t RLE_(end)(RLE_T *r)
{
    if (not_ready(r))
        return RLE_ERROR;
    do_end(r);
    if (r->out && !r->err)
        (void)drain_out(r);
    return RLE_(state)(r);
}

rle_status_t RLE_(state)(RLE_T *r)
{
    if (not_ready(r) || r->err)
        return RLE_ERROR;
    if (r->has_held || r->ops->pending(r))
        return RLE_OK;
    return r->ended ? RLE_END : RLE_AGAIN;
}

rle_err_t RLE_(error)(const RLE_T *r)
{
    return (rle_err_t)r->err;
}

/* ======================= One-shot functions ======================= */

size_t RLE_(bound)(size_t n)
{
    return n > ((size_t)-1) / 2u ? (size_t)-1 : 2u * n;
}

static rle_err_t run_buf(RLE_T *r, const SYM_T *in, size_t n,
                         SYM_T *out, size_t cap, size_t *out_len)
{
    size_t i = 0, o = 0;
    SYM_T  s;

    for (;;) {
        while (get_sym(r, &s)) {
            if (out) {
                if (o >= cap) {
                    fail(r, RLE_E_SPACE);
                    goto done;
                }
                out[o] = s;
            }
            ++o;
        }
        if (r->err)
            break;
        if (i < n)
            r->ops->put(r, in[i++]);
        else if (!r->ended)
            do_end(r);
        else
            break;
    }
done:
    if (out_len)
        *out_len = o;
    return (rle_err_t)r->err;
}

/* config for one-shot functions: copy without callbacks */
static void buf_cfg(RLE_CFG_T *c, const RLE_CFG_T *cfg)
{
    if (cfg)
        *c = *cfg;
    else
        memset(c, 0, sizeof *c);
    c->in  = NULL;
    c->out = NULL;
}

rle_err_t RLE_(encode_buf)(const RLE_CFG_T *cfg, const SYM_T *in, size_t n,
                           SYM_T *out, size_t cap, size_t *out_len)
{
    RLE_CFG_T c;
    RLE_T     r;

    if (out_len)
        *out_len = 0;
    if (!in && n)
        return RLE_E_PARAM;
    buf_cfg(&c, cfg);
    if (RLE_(enc_init)(&r, &c) != RLE_OK)
        return RLE_(error)(&r);
    return run_buf(&r, in, n, out, cap, out_len);
}

rle_err_t RLE_(decode_buf)(const RLE_CFG_T *cfg, const SYM_T *in, size_t n,
                           SYM_T *out, size_t cap, size_t *out_len)
{
    RLE_CFG_T c;
    RLE_T     r;

    if (out_len)
        *out_len = 0;
    if (!in && n)
        return RLE_E_PARAM;
    buf_cfg(&c, cfg);
    if (RLE_(dec_init)(&r, &c) != RLE_OK)
        return RLE_(error)(&r);
    return run_buf(&r, in, n, out, cap, out_len);
}

/* ============================ Adapters ============================ */

int RLE_(mem_in)(void *user, SYM_T *s)
{
    RLE_MEM_T *m = (RLE_MEM_T *)user;
    if (m->pos >= m->len)
        return RLE_IN_EOF;
    *s = m->rd[m->pos++];
    return RLE_IN_DATA;
}

int RLE_(mem_out)(void *user, SYM_T s)
{
    RLE_MEM_T *m = (RLE_MEM_T *)user;
    if (m->pos >= m->len)
        return 0;
    m->wr[m->pos++] = s;
    return 1;
}

void RLE_(ring_init)(RLE_RING_T *q, SYM_T *buf, size_t size)
{
    q->buf    = buf;
    q->size   = size;
    q->head   = 0;
    q->tail   = 0;
    q->closed = 0;
}

int RLE_(ring_put)(RLE_RING_T *q, SYM_T s)
{
    size_t h = q->head;
    size_t n = (h + 1 == q->size) ? 0 : h + 1;
    if (n == q->tail)
        return 0;
    q->buf[h] = s;
    q->head   = n;
    return 1;
}

int RLE_(ring_get)(RLE_RING_T *q, SYM_T *s)
{
    size_t t = q->tail;
    if (t == q->head)
        return 0;
    *s      = q->buf[t];
    q->tail = (t + 1 == q->size) ? 0 : t + 1;
    return 1;
}

size_t RLE_(ring_count)(const RLE_RING_T *q)
{
    size_t h = q->head, t = q->tail;
    return (h >= t) ? h - t : q->size - t + h;
}

void RLE_(ring_close)(RLE_RING_T *q)
{
    q->closed = 1;
}

int RLE_(ring_in)(void *user, SYM_T *s)
{
    RLE_RING_T *q = (RLE_RING_T *)user;
    if (RLE_(ring_get)(q, s))
        return RLE_IN_DATA;
    if (!q->closed)
        return RLE_IN_WAIT;
    /* closed was set after the last put: check once more */
    return RLE_(ring_get)(q, s) ? RLE_IN_DATA : RLE_IN_EOF;
}

int RLE_(ring_out)(void *user, SYM_T s)
{
    return RLE_(ring_put)((RLE_RING_T *)user, s);
}

#undef RLE_T
#undef RLE_CFG_T
#undef RLE_MEM_T
#undef RLE_RING_T
