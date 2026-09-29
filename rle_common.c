/* rle_common.c - shared helper functions */
#include "rle_common.h"

const char *rle_strerror(rle_err_t e)
{
    switch (e) {
    case RLE_E_NONE:      return "no error";
    case RLE_E_PARAM:     return "invalid parameter";
    case RLE_E_STATE:     return "call does not match operating mode";
    case RLE_E_TRUNCATED: return "data truncated";
    case RLE_E_SPACE:     return "output buffer too small";
    default:              return "unknown error";
    }
}
