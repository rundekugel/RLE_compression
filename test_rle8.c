/* test_rle8.c - tests for rle8 (logic in test_core.inc) */
#include "rle8.h"
#define RLE_(n)        rle8_##n
#define rle8_MEM_SRC_(p, n) RLE8_MEM_SRC(p, n)
#define rle8_MEM_DST_(p, n) RLE8_MEM_DST(p, n)
#define SYM_T          uint8_t
#define IND_T          uint16_t
#define SYM_MAX        0xFFu
#define W_BITS         8
#define DEF_IND        RLE8_DEFAULT_INDICATOR
#define MAX_RUN        RLE8_MAX_RUN
#define IND_FN(v)      RLE8_IND(v)
#define TEST_ENTRY     test_rle8
#include "test_core.inc"
