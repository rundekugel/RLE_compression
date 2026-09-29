/*
 * test_main.c - runs the test suites for rle8 and rle16
 * Build: gcc -std=c99 -Wall -Wextra -O2 rle_common.c rle8.c rle16.c \
 *            test_rle8.c test_rle16.c test_main.c -o test_rle
 */
#include <stdio.h>

int test_rle8(int *checks);
int test_rle16(int *checks);

int main(void)
{
    int c8, c16;
    int f = test_rle8(&c8);
    printf("\n");
    f += test_rle16(&c16);
    printf("\n%d checks, %d failed\n", c8 + c16, f);
    return f ? 1 : 0;
}
