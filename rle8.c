/* rle8.c - instance for 8-bit elements; logic in rle_core.inc */
#include "rle8.h"

#define RLE_(n)      rle8_##n
#define SYM_T        uint8_t
#define CNT_T        uint8_t
#define IND_T        uint16_t
#define SYM_MAX      0xFFu
#define IND_FLAG     0x100u
#define MAX_RUN      RLE8_MAX_RUN
#define DEFAULT_IND  RLE8_DEFAULT_INDICATOR

#include "rle_core.inc"
