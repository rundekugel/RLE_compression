/*
 * example_rle8.c - Simple example: push data into the encoder byte by byte,
 *                  draining all available output after each byte.
 *
 * Compile:
 *   gcc -Wall -o example_rle8 example_rle8.c rle8.c rle_common.c
 */

#include <stdio.h>
#include "rle8.h"

static void drain(rle8_t *enc)
{
    uint8_t byte;
    while (rle8_read(enc, &byte, 1) == 1)
        printf("%02X. ", byte);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);   /* unbuffered stdout -> immediately visible */

    /* Source data as a concatenated string (sizeof - 1 to exclude the trailing '\0').
     * Split into separate segments because of hex escapes before hex-capable chars (e.g. 'e' in "ende"). */
    static const uint8_t src[] =
        "start-0123456789"                                           /* text          */
        "\x7f\x80\x81\x82\x8e\x8f"      /* some data */
        "\xf9\xfa\xfb\xfc\xfd\xfe\xff"
        "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f"
        "\xa5"                                /*1x indicator*/
        "\x10\x11\x12\x13\x14"
        "\xa5\xA5"                                /*2x indicator*/
        "\x32\x32\x32\x32\x32"                  /* 5x 50        */
        "\xa5\xa5\xa5\xa5\xa5"                  /* 5x indicator */
        "end";
    const size_t n = sizeof(src) - 1;   /* exclude trailing '\0' */

    printf("Source data: %zu bytes\n\n", n);

    /* initialize encoder */
    rle8_t enc;
    rle8_enc_init(&enc, NULL);   /* NULL = default configuration */

    printf("Encoded:\n");

    /* main loop: one byte in, drain everything out */
    for (size_t i = 0; i < n; i++) {
        rle8_write(&enc, &src[i], 1);
        drain(&enc);
    }

    /* signal end of input -> flush remaining buffered data */
    rle8_end(&enc);
    drain(&enc);

    printf("\n\nStatus: %s\n",
           rle8_state(&enc) == RLE_END ? "RLE_END (ok)" : "incomplete?");

    return 0;
}
