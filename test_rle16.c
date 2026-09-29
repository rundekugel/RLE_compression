/* test_rle16.c - tests for rle16 (logic in test_core.inc) */
#include "rle16.h"
#define RLE_(n)        rle16_##n
#define rle16_MEM_SRC_(p, n) RLE16_MEM_SRC(p, n)
#define rle16_MEM_DST_(p, n) RLE16_MEM_DST(p, n)
#define SYM_T          uint16_t
#define IND_T          uint32_t
#define SYM_MAX        0xFFFFu
#define W_BITS         16
#define DEF_IND        RLE16_DEFAULT_INDICATOR
#define MAX_RUN        RLE16_MAX_RUN
#define IND_FN(v)      RLE16_IND(v)
#define TEST_ENTRY     test_rle16
#include "test_core.inc"
