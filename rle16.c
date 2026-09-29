/* rle16.c - instance for 16-bit elements (e.g. RGB565); logic in rle_core.inc */
#include "rle16.h"

#define RLE_(n)      rle16_##n
#define SYM_T        uint16_t
#define CNT_T        uint16_t
#define IND_T        uint32_t
#define SYM_MAX      0xFFFFu
#define IND_FLAG     0x10000ul
#define MAX_RUN      RLE16_MAX_RUN
#define DEFAULT_IND  RLE16_DEFAULT_INDICATOR

#include "rle_core.inc"
