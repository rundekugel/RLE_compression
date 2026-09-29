/*
 * test_core.inc - tests, included once per element width (test_rle8.c / test_rle16.c)
 * Expected macros: RLE_(n), SYM_T, IND_T, SYM_MAX, W_BITS, DEF_IND, MAX_RUN,
 *                  IND_FN(v), TEST_ENTRY
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define T_T    RLE_(t)
#define T_CFG  RLE_(cfg_t)

static int g_checks, g_fail;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; \
    if (g_fail < 30) printf("FAIL [%d bit] line %d: %s\n", W_BITS, __LINE__, #c); } } while (0)

static uint32_t g_rng = 12345u;
static unsigned rnd(unsigned n)
{
    g_rng = g_rng * 1103515245u + 12345u;
    return (unsigned)(g_rng >> 16) % n;
}
static SYM_T rnd_sym(void)
{
    unsigned v = rnd(256);
#if W_BITS == 16
    v |= rnd(256) << 8;
#endif
    return (SYM_T)v;
}
static size_t min_sz(size_t a, size_t b) { return a < b ? a : b; }
static void fill(SYM_T *p, SYM_T v, size_t n) { size_t i; for (i = 0; i < n; ++i) p[i] = v; }
static int same(const SYM_T *a, const SYM_T *b, size_t n) { return n == 0 || memcmp(a, b, n * sizeof(SYM_T)) == 0; }

typedef enum { T_ENC, T_DEC } tmode_t;
static rle_status_t tinit(T_T *r, tmode_t m, const T_CFG *cfg)
{
    return m == T_ENC ? RLE_(enc_init)(r, cfg) : RLE_(dec_init)(r, cfg);
}

/* ---- test source/sink with random waits and refusals ---- */
typedef struct { const SYM_T *p; size_t n, pos; unsigned wait_pct; int eof_calls; } tsrc_t;
static int tsrc_in(void *u, SYM_T *s)
{
    tsrc_t *q = (tsrc_t *)u;
    if (q->pos >= q->n) { q->eof_calls++; return RLE_IN_EOF; }
    if (rnd(100) < q->wait_pct) return RLE_IN_WAIT;
    *s = q->p[q->pos++];
    return RLE_IN_DATA;
}

typedef struct { SYM_T *p; size_t cap, len; unsigned refuse_pct; int overflow; } tsink_t;
static int tsink_out(void *u, SYM_T s)
{
    tsink_t *q = (tsink_t *)u;
    if (rnd(100) < q->refuse_pct) return 0;
    if (q->len >= q->cap) { q->overflow = 1; return 0; }
    q->p[q->len++] = s;
    return 1;
}

static void append(SYM_T *out, size_t cap, size_t *o, const SYM_T *src, size_t k)
{
    CHECK(*o + k <= cap);
    if (*o + k <= cap) memcpy(out + *o, src, k * sizeof(SYM_T));
    *o += k;
}

/* ---- operating modes ---- */
enum { M_BUF, M_RW, M_PUSH, M_PULL, M_PUMP, M_RING, M_COUNT };
static const char *const k_mname[M_COUNT] = { "buf", "write/read", "push+cb", "pull", "pump", "ring" };

#define GUARD_MAX 50000000ul

static rle_err_t process(tmode_t mode, const T_CFG *cfg0, int method,
                         const SYM_T *in, size_t n, SYM_T *out, size_t cap, size_t *olen)
{
    T_CFG         cfg = *cfg0;
    T_T           r;
    rle_status_t  st;
    size_t        i = 0, o = 0, k;
    unsigned long guard = 0;
    SYM_T         tmp[16];
    int           ended = 0;

    *olen = 0;
    if (method == M_BUF)
        return mode == T_ENC ? RLE_(encode_buf)(&cfg, in, n, out, cap, olen)
                             : RLE_(decode_buf)(&cfg, in, n, out, cap, olen);

    if (method == M_RW) {                                   /* both sides active */
        if (tinit(&r, mode, &cfg) != RLE_OK) return RLE_(error)(&r);
        for (;;) {
            if (++guard > GUARD_MAX) { CHECK(!"guard"); break; }
            if (i < n)          i += RLE_(write)(&r, in + i, min_sz(n - i, 1 + rnd(7)));
            else if (!ended)    { RLE_(end)(&r); ended = 1; }
            k = RLE_(read)(&r, tmp, 1 + rnd(9));
            append(out, cap, &o, tmp, k);
            st = RLE_(state)(&r);
            if (st == RLE_END || st == RLE_ERROR) break;
        }
        *olen = o;
        return RLE_(error)(&r);
    }

    if (method == M_PUSH) {                                 /* source master, sink callback */
        tsink_t sk;
        sk.p = out; sk.cap = cap; sk.len = 0; sk.refuse_pct = 30; sk.overflow = 0;
        cfg.out = tsink_out; cfg.out_user = &sk;
        if (tinit(&r, mode, &cfg) != RLE_OK) return RLE_(error)(&r);
        for (;;) {
            if (++guard > GUARD_MAX) { CHECK(!"guard"); break; }
            if (i < n) {
                size_t c = min_sz(n - i, 1 + rnd(7));
                size_t w = RLE_(write)(&r, in + i, c);
                i += w;
                if (w < c) (void)RLE_(run)(&r);             /* drain backpressure */
                if (RLE_(state)(&r) == RLE_ERROR) break;
            } else {
                if (!ended) { RLE_(end)(&r); ended = 1; }
                st = RLE_(run)(&r);
                if (st == RLE_END || st == RLE_ERROR) break;
            }
        }
        CHECK(!sk.overflow);
        *olen = sk.len;
        return RLE_(error)(&r);
    }

    if (method == M_PULL) {                                 /* sink master, source callback */
        tsrc_t src;
        src.p = in; src.n = n; src.pos = 0; src.wait_pct = 25; src.eof_calls = 0;
        cfg.in = tsrc_in; cfg.in_user = &src;
        if (tinit(&r, mode, &cfg) != RLE_OK) return RLE_(error)(&r);
        for (;;) {
            if (++guard > GUARD_MAX) { CHECK(!"guard"); break; }
            k = RLE_(read)(&r, tmp, 1 + rnd(9));
            append(out, cap, &o, tmp, k);
            st = RLE_(state)(&r);
            if (st == RLE_END || st == RLE_ERROR) break;
        }
        CHECK(src.eof_calls <= 1);                          /* source not queried after EOF */
        *olen = o;
        return RLE_(error)(&r);
    }

    if (method == M_PUMP) {                                 /* both callbacks */
        tsrc_t  src;
        tsink_t sk;
        src.p = in;  src.n = n;     src.pos = 0; src.wait_pct = 25;   src.eof_calls = 0;
        sk.p  = out; sk.cap = cap;  sk.len = 0;  sk.refuse_pct = 25;  sk.overflow = 0;
        cfg.in = tsrc_in;     cfg.in_user  = &src;
        cfg.out = tsink_out;  cfg.out_user = &sk;
        if (tinit(&r, mode, &cfg) != RLE_OK) return RLE_(error)(&r);
        do {
            if (++guard > GUARD_MAX) { CHECK(!"guard"); break; }
            st = RLE_(run)(&r);
        } while (st == RLE_AGAIN);
        CHECK(st == RLE_END || st == RLE_ERROR);
        CHECK(!sk.overflow);
        *olen = sk.len;
        return RLE_(error)(&r);
    }

    /* M_RING: ring buffer adapter, producer and consumer run "concurrently" */
    {
        SYM_T        ib[8], ob[5], s;
        RLE_(ring_t) ri, ro;
        unsigned     j, m;
        RLE_(ring_init)(&ri, ib, 8);
        RLE_(ring_init)(&ro, ob, 5);
        cfg.in = RLE_(ring_in);   cfg.in_user  = &ri;
        cfg.out = RLE_(ring_out); cfg.out_user = &ro;
        if (tinit(&r, mode, &cfg) != RLE_OK) return RLE_(error)(&r);
        for (;;) {
            if (++guard > GUARD_MAX) { CHECK(!"guard"); break; }
            for (m = rnd(6), j = 0; j < m && i < n && RLE_(ring_put)(&ri, in[i]); ++j) ++i;
            if (i == n && !ri.closed) RLE_(ring_close)(&ri);
            st = RLE_(run)(&r);
            for (m = rnd(6), j = 0; j < m && RLE_(ring_get)(&ro, &s); ++j) append(out, cap, &o, &s, 1);
            if (st == RLE_ERROR) break;
            if (st == RLE_END && RLE_(ring_count)(&ro) == 0) break;
        }
        *olen = o;
        return RLE_(error)(&r);
    }
}

/* ---- Round-trip: every mode must produce identical output to the one-shot function ---- */
static size_t roundtrip(const T_CFG *cfg, const SYM_T *in, size_t n)
{
    size_t cap = RLE_(bound)(n);
    SYM_T *ref = (SYM_T *)malloc((cap + 1) * sizeof(SYM_T));
    SYM_T *enc = (SYM_T *)malloc((cap + 16) * sizeof(SYM_T));
    SYM_T *dec = (SYM_T *)malloc((n + 16) * sizeof(SYM_T));
    size_t rl, l;
    int    m;

    CHECK(RLE_(encode_buf)(cfg, in, n, ref, cap, &rl) == RLE_E_NONE);
    CHECK(rl <= cap);
    CHECK(RLE_(encode_buf)(cfg, in, n, NULL, 0, &l) == RLE_E_NONE && l == rl);

    for (m = 0; m < M_COUNT; ++m) {
        CHECK(process(T_ENC, cfg, m, in, n, enc, cap + 16, &l) == RLE_E_NONE);
        CHECK(l == rl && same(enc, ref, rl));
        if (l != rl || !same(enc, ref, rl)) printf("  encoder %s differs (n=%zu)\n", k_mname[m], n);
    }
    for (m = 0; m < M_COUNT; ++m) {
        CHECK(process(T_DEC, cfg, m, ref, rl, dec, n + 16, &l) == RLE_E_NONE);
        CHECK(l == n && same(dec, in, n));
        if (l != n || !same(dec, in, n)) printf("  decoder %s differs (n=%zu)\n", k_mname[m], n);
    }
    CHECK(RLE_(decode_buf)(cfg, ref, rl, NULL, 0, &l) == RLE_E_NONE && l == n);
    free(ref); free(enc); free(dec);
    return rl;
}

static size_t gen_runs(SYM_T *buf, size_t max, SYM_T ind)
{
    size_t n = 0, len;
    SYM_T  v;
    while (n + 2600 < max) {
        len = 1 + (rnd(20) == 0 ? rnd(2500) : rnd(4) == 0 ? rnd(512) : rnd(6));
        v   = rnd(5) == 0 ? ind : rnd_sym();
        fill(buf + n, v, len);
        n += len;
    }
    return n;
}

#if W_BITS == 16
#define RGB565(r, g, b) ((SYM_T)(((unsigned)(r) << 11) | ((unsigned)(g) << 5) | (unsigned)(b)))
#define DW 320u
#define DH 240u

/* UI-like image: background, panels, border, "text" area */
static size_t gen_ui(SYM_T *p)
{
    unsigned x, y;
    for (y = 0; y < DH; ++y)
        for (x = 0; x < DW; ++x) {
            SYM_T c = RGB565(2, 4, 6);                                  /* background */
            if (y < 24)                                c = RGB565(0, 20, 28);   /* title bar */
            if (x >= 20 && x < 150 && y >= 40 && y < 110) c = RGB565(31, 40, 0); /* button */
            if (x >= 170 && x < 300 && y >= 40 && y < 110) c = RGB565(0, 50, 10);
            if ((x == 19 || x == 150) && y >= 40 && y < 110) c = 0xFFFFu;       /* border */
            if (x >= 20 && x < 300 && y >= 130 && y < 220 && rnd(5) == 0) c = 0xFFFFu; /* text */
            p[y * DW + x] = c;
        }
    return DW * DH;
}

static size_t gen_gradient(SYM_T *p)
{
    unsigned x, y;
    for (y = 0; y < DH; ++y)
        for (x = 0; x < DW; ++x)
            p[y * DW + x] = RGB565(x * 31u / (DW - 1), y * 63u / (DH - 1), (x + y) * 31u / (DW + DH - 2));
    return DW * DH;
}
#endif

static void test_roundtrips(void)
{
    static SYM_T buf[80000];
    const IND_T  inds[]  = { 0, IND_FN(0), (IND_T)SYM_MAX };
    const uint8_t mruns[] = { 0, 2, 3, 8, 255 };
#if W_BITS == 16
    const size_t runs[] = { 1, 2, 3, 4, 254, 255, 256, 1000, 65535, 65536, 70000 };
#else
    const size_t runs[] = { 1, 2, 3, 4, 254, 255, 256, 511, 1000 };
#endif
    size_t ii, mi, k, i, n;
    int    fail0 = g_fail;

    for (ii = 0; ii < sizeof inds / sizeof inds[0]; ++ii)
    for (mi = 0; mi < sizeof mruns; ++mi) {
        T_CFG cfg;
        SYM_T ind;
        memset(&cfg, 0, sizeof cfg);
        cfg.indicator = inds[ii];
        cfg.min_run   = mruns[mi];
        ind = inds[ii] == 0 ? (SYM_T)DEF_IND : (SYM_T)(inds[ii] & SYM_MAX);

        roundtrip(&cfg, buf, 0);
        for (k = 0; k < sizeof runs / sizeof runs[0]; ++k) {
            fill(buf, (SYM_T)(ind ^ 0x5A), runs[k]); roundtrip(&cfg, buf, runs[k]);
            fill(buf, ind, runs[k]);                roundtrip(&cfg, buf, runs[k]);
        }
        for (i = 0; i < 1000; ++i) buf[i] = (SYM_T)i;
        roundtrip(&cfg, buf, 1000);
        for (i = 0; i < 600; ++i) buf[i] = (i & 1) ? ind : (SYM_T)i;
        roundtrip(&cfg, buf, 600);
        for (i = 0; i < 600; ++i) buf[i] = (i % 3) ? ind : (SYM_T)0x33;
        roundtrip(&cfg, buf, 600);
        for (i = 0; i < 2000; ++i) buf[i] = rnd_sym();
        roundtrip(&cfg, buf, 2000);
        for (k = 0; k < 3; ++k) { n = gen_runs(buf, 20000, ind); roundtrip(&cfg, buf, n); }
#if W_BITS == 16
        roundtrip(&cfg, buf, gen_ui(buf));
        roundtrip(&cfg, buf, gen_gradient(buf));
#endif
    }
    printf("[%2d bit] %-32s %s\n", W_BITS, "Round-trips (15 configs x 6 modes)", g_fail == fail0 ? "ok" : "FAILED");

#if W_BITS == 16
    {   /* informational: compression ratio for RGB565 with default config */
        T_CFG c;
        size_t l;
        memset(&c, 0, sizeof c);
        n = gen_ui(buf);
        RLE_(encode_buf)(&c, buf, n, NULL, 0, &l);
        printf("[%2d bit]   UI 320x240:        %6zu -> %6zu pixels (ratio %.1f)\n", W_BITS, n, l, (double)n / (double)l);
        n = gen_gradient(buf);
        RLE_(encode_buf)(&c, buf, n, NULL, 0, &l);
        printf("[%2d bit]   gradient 320x240:  %6zu -> %6zu pixels (ratio %.2f)\n", W_BITS, n, l, (double)n / (double)l);
        fill(buf, RGB565(0, 0, 31), DW * DH);
        RLE_(encode_buf)(&c, buf, DW * DH, NULL, 0, &l);
        printf("[%2d bit]   solid 320x240:     %6u -> %6zu pixels\n", W_BITS, DW * DH, l);
    }
#endif
}

/* ---- fixed test vectors ---- */
static void expect(const char *name, const T_CFG *cfg, const SYM_T *in, size_t n,
                   const SYM_T *ex, size_t en)
{
    SYM_T  out[64];
    size_t l;
    int    ok = RLE_(encode_buf)(cfg, in, n, out, 64, &l) == RLE_E_NONE &&
                l == en && same(out, ex, en);
    CHECK(ok);
    printf("[%2d bit] %-32s %s\n", W_BITS, name, ok ? "ok" : "FAILED");
}

static void test_vectors(void)
{
    static SYM_T big[MAX_RUN + 45];
    const SYM_T I = (SYM_T)DEF_IND, v = 0x11;
    T_CFG z;
    memset(&z, 0, sizeof z);
    z.indicator = IND_FN(0);

    { SYM_T in[] = { v, v, v };        SYM_T ex[] = { v, v, v };   expect("3x value -> literals", NULL, in, 3, ex, 3); }
    { SYM_T in[] = { v, v, v, v, v };  SYM_T ex[] = { I, 5, v };   expect("5x value -> run", NULL, in, 5, ex, 3); }
    { SYM_T ex[] = { I, (SYM_T)MAX_RUN, v, I, 45, v };
      fill(big, v, MAX_RUN + 45);                                  expect("MAX_RUN+45 -> 2 runs", NULL, big, MAX_RUN + 45, ex, 6); }
    { SYM_T in[] = { I };              SYM_T ex[] = { I, 0 };      expect("1x IND -> IND 0", NULL, in, 1, ex, 2); }
    { SYM_T in[] = { I, I };           SYM_T ex[] = { I, 2, I };   expect("2x IND -> run", NULL, in, 2, ex, 3); }
    { SYM_T in[] = { 0, v };           SYM_T ex[] = { 0, 0, v };   expect("indicator 0 explicit", &z, in, 2, ex, 3); }
#if W_BITS == 16
    CHECK(DEF_IND == 0xCAFEu);
#else
    CHECK(DEF_IND == 0xA5u);
#endif
}

/* ---- error cases ---- */
static void expect_dec_err(const SYM_T *in, size_t n, rle_err_t e)
{
    T_CFG  cfg;
    SYM_T  out[64];
    size_t l;
    int    m;
    memset(&cfg, 0, sizeof cfg);
    for (m = 0; m < M_COUNT; ++m) {
        rle_err_t got = process(T_DEC, &cfg, m, in, n, out, 64, &l);
        CHECK(got == e);
        if (got != e) printf("  mode %s: %s instead of %s\n", k_mname[m], rle_strerror(got), rle_strerror(e));
    }
}

static void test_errors(void)
{
    const SYM_T I = (SYM_T)DEF_IND;
    T_CFG  cfg;
    T_T    r;
    SYM_T  s = 0, out[16];
    size_t l;
    int    e, fail0 = g_fail;

    { SYM_T in[] = { I };    expect_dec_err(in, 1, RLE_E_TRUNCATED); }
    { SYM_T in[] = { I, 5 }; expect_dec_err(in, 2, RLE_E_TRUNCATED); }

    /* configuration */
    memset(&cfg, 0, sizeof cfg);
    cfg.min_run = 1;                 CHECK(RLE_(enc_init)(&r, &cfg) == RLE_ERROR && RLE_(error)(&r) == RLE_E_PARAM);
    cfg.min_run = 1;                 CHECK(RLE_(dec_init)(&r, &cfg) == RLE_OK);        /* decoder ignores min_run */
    cfg.min_run = 0;
    cfg.indicator = (IND_T)(IND_FN(SYM_MAX) + 1);
                                     CHECK(RLE_(enc_init)(&r, &cfg) == RLE_ERROR && RLE_(error)(&r) == RLE_E_PARAM);
                                     CHECK(RLE_(dec_init)(&r, &cfg) == RLE_ERROR);
    cfg.indicator = IND_FN(SYM_MAX); CHECK(RLE_(enc_init)(&r, &cfg) == RLE_OK);
    CHECK(RLE_(enc_init)(&r, NULL) == RLE_OK);
    CHECK(RLE_(enc_init)(NULL, NULL) == RLE_ERROR);

    /* one-shot functions */
    { SYM_T in[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
      CHECK(RLE_(encode_buf)(NULL, in, 10, out, 5, &l) == RLE_E_SPACE && l == 5); }
    CHECK(RLE_(encode_buf)(NULL, NULL, 3, out, 16, &l) == RLE_E_PARAM);
    CHECK(RLE_(encode_buf)(NULL, NULL, 0, out, 16, &l) == RLE_E_NONE && l == 0);
    CHECK(RLE_(bound)(((size_t)-1) / 2u + 1u) == (size_t)-1);                       /* overflow protection */

    /* misuse */
    RLE_(enc_init)(&r, NULL);
    RLE_(end)(&r);
    CHECK(RLE_(write)(&r, &s, 1) == 0 && RLE_(error)(&r) == RLE_E_STATE);
    RLE_(enc_init)(&r, NULL);
    CHECK(RLE_(run)(&r) == RLE_ERROR && RLE_(error)(&r) == RLE_E_STATE);           /* no out configured */
    { RLE_(mem_t) m = RLE_(MEM_DST_)(out, 16); memset(&cfg, 0, sizeof cfg);
      cfg.out = RLE_(mem_out); cfg.out_user = &m; RLE_(enc_init)(&r, &cfg);
      CHECK(RLE_(read)(&r, &s, 1) == 0 && RLE_(error)(&r) == RLE_E_STATE); }         /* read with cfg.out set */
    { RLE_(mem_t) m = RLE_(MEM_SRC_)(out, 1); memset(&cfg, 0, sizeof cfg);
      cfg.in = RLE_(mem_in); cfg.in_user = &m; RLE_(enc_init)(&r, &cfg);
      CHECK(RLE_(write)(&r, &s, 1) == 0 && RLE_(error)(&r) == RLE_E_STATE); }        /* write with cfg.in set */
    memset(&r, 0, sizeof r);                                                         /* uninitialized */
    CHECK(RLE_(write)(&r, &s, 1) == 0 && RLE_(error)(&r) == RLE_E_STATE);
    CHECK(RLE_(state)(&r) == RLE_ERROR);

    for (e = RLE_E_NONE; e <= RLE_E_SPACE + 1; ++e) CHECK(rle_strerror((rle_err_t)e) != NULL);
    printf("[%2d bit] %-32s %s\n", W_BITS, "Error cases", g_fail == fail0 ? "ok" : "FAILED");
}

/* ---- backpressure, status, adapters ---- */
static void test_behaviour(void)
{
    const SYM_T I = (SYM_T)DEF_IND;
    T_T    r;
    SYM_T  s, out[64];
    int    fail0 = g_fail;

    /* decoder: run blocks further input until drained */
    RLE_(dec_init)(&r, NULL);
    { SYM_T in[] = { I, 3, 0x42, 0x43 };
      CHECK(RLE_(write)(&r, in, 4) == 3);
      CHECK(RLE_(state)(&r) == RLE_OK);
      CHECK(RLE_(read)(&r, out, 2) == 2 && out[0] == 0x42 && out[1] == 0x42);
      CHECK(RLE_(write)(&r, in + 3, 1) == 0);                       /* 1 element still pending */
      CHECK(RLE_(read)(&r, out, 8) == 1);
      CHECK(RLE_(state)(&r) == RLE_AGAIN);
      CHECK(RLE_(write)(&r, in + 3, 1) == 1);
      CHECK(RLE_(end)(&r) == RLE_OK);                               /* 0x43 still buffered */
      CHECK(RLE_(read)(&r, &s, 1) == 1 && s == 0x43);
      CHECK(RLE_(state)(&r) == RLE_END); }

    /* memory adapter: full sink -> AGAIN, not END */
    { SYM_T in[] = { 1, 2, 3, 4, 5, 6 }; SYM_T small[3];
      RLE_(mem_t) src = RLE_(MEM_SRC_)(in, 6), dst = RLE_(MEM_DST_)(small, 3);
      T_CFG c; memset(&c, 0, sizeof c);
      c.in = RLE_(mem_in); c.in_user = &src; c.out = RLE_(mem_out); c.out_user = &dst;
      RLE_(enc_init)(&r, &c);
      CHECK(RLE_(run)(&r) == RLE_AGAIN && dst.pos == 3);
      CHECK(RLE_(state)(&r) == RLE_OK);                              /* output waiting */
      dst.pos = 0;                                                   /* sink drained */
      CHECK(RLE_(run)(&r) == RLE_END && dst.pos == 3 && small[0] == 4); }

    /* ring buffer */
    { SYM_T rb[4]; RLE_(ring_t) q; int i;
      RLE_(ring_init)(&q, rb, 4);
      for (i = 0; i < 3; ++i) CHECK(RLE_(ring_put)(&q, (SYM_T)i) == 1);
      CHECK(RLE_(ring_put)(&q, 9) == 0 && RLE_(ring_count)(&q) == 3);
      CHECK(RLE_(ring_in)(&q, &s) == RLE_IN_DATA && s == 0);
      CHECK(RLE_(ring_get)(&q, &s) && s == 1 && RLE_(ring_get)(&q, &s) && s == 2);
      CHECK(RLE_(ring_in)(&q, &s) == RLE_IN_WAIT);
      RLE_(ring_close)(&q);
      CHECK(RLE_(ring_in)(&q, &s) == RLE_IN_EOF); }

    printf("[%2d bit] %-32s %s\n", W_BITS, "Backpressure/status/adapters", g_fail == fail0 ? "ok" : "FAILED");
}

int TEST_ENTRY(int *checks)
{
    test_vectors();
    test_errors();
    test_behaviour();
    test_roundtrips();
    *checks = g_checks;
    return g_fail;
}
