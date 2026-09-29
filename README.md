# RLE Compression

A lightweight C99 RLE (Run-Length Encoding) library for 8-bit and 16-bit data streams. Designed for both hosted and embedded targets (e.g. ARM Cortex-M).

## Features

- `rle8` — encodes/decodes streams of `uint8_t` elements
- `rle16` — encodes/decodes streams of `uint16_t` elements
- Streaming API with backpressure support
- One-shot buffer API
- Built-in memory buffer and ring buffer adapters
- No dynamic allocation, no dependencies beyond `<stdint.h>` / `<stddef.h>`
- Dead code elimination via `-ffunction-sections` / `--gc-sections`
- MIT licensed

## Wire Format

```
<s>        Literal element (s != indicator)
IND 0      Escaped literal of the indicator value itself
IND n v    Run: value v repeated n times (n = 1..255 for rle8, 1..65535 for rle16)
```

Default indicators: `0xA5` (rle8), `0xCAFE` (rle16). Both are configurable. There is no file header — the caller manages indicator and length metadata.

## Build

```sh
make            # builds build/librle.a
make test       # builds build/test_rle
make check      # builds and runs tests
make sanitize   # runs tests with AddressSanitizer + UBSan
make clean
```

Override variables as needed:

```sh
make CC=arm-none-eabi-gcc CFLAGS="-mcpu=cortex-m4 -mthumb" OPT=-Os
make WERROR=1
make BUILD=out
```

## API Overview

All functions exist in `rle8_*` and `rle16_*` variants. The examples below use `rle8`.

### One-shot (buffer to buffer)

```c
#include "rle8.h"

uint8_t out[RLE8_BOUND(sizeof(src))];
size_t out_len;

rle8_encode_buf(NULL, src, sizeof(src), out, sizeof(out), &out_len);
rle8_decode_buf(NULL, out, out_len, dec, sizeof(dec), &dec_len);
```

Pass `NULL` for `cfg` to use defaults. Pass `out = NULL` to query the required size without writing.

### Streaming

Four source/sink combinations are supported:

| Source       | Sink       | Driver call                        |
|--------------|------------|------------------------------------|
| `rle8_write` | `rle8_read`| `rle8_write()` + `rle8_read()`    |
| `rle8_write` | `cfg.out`  | `rle8_write()` + `rle8_run()`     |
| `cfg.in`     | `rle8_read`| `rle8_read()`                      |
| `cfg.in`     | `cfg.out`  | `rle8_run()`                       |

```c
rle8_t enc;
rle8_enc_init(&enc, NULL);          // NULL = default config

rle8_write(&enc, data, len);        // returns number of elements accepted
rle8_end(&enc);                     // signal end of input

uint8_t buf[64];
size_t got = rle8_read(&enc, buf, sizeof(buf));

rle_status_t s = rle8_state(&enc);  // RLE_OK, RLE_AGAIN, RLE_END, RLE_ERROR
```

### Configuration

```c
rle8_cfg_t cfg = {
    .indicator = RLE8_IND(0x00),  // use 0x00 as indicator (RLE8_IND needed for value 0)
    .min_run   = 3,               // encode runs of 3+ as a run (default: 4)
    .in        = my_read_fn,      // optional source callback
    .in_user   = &my_ctx,
    .out       = my_write_fn,     // optional sink callback
    .out_user  = &my_ctx,
};
rle8_enc_init(&enc, &cfg);
```

### Ring Buffer Adapter

For ISR ↔ main-loop use on single-core MCUs:

```c
uint8_t raw[64];
rle8_ring_t ring;
rle8_ring_init(&ring, raw, sizeof(raw));

rle8_ring_put(&ring, byte);         // from producer (ISR)
rle8_ring_get(&ring, &byte);        // from consumer

rle8_ring_close(&ring);             // producer done; rle8_ring_in returns EOF

// use as cfg.in / cfg.out:
cfg.in      = rle8_ring_in;
cfg.in_user = &ring;
```

> On multi-core systems, add memory barriers around ring buffer access.

## Status and Errors

```c
rle_status_t  // RLE_OK, RLE_AGAIN, RLE_END, RLE_ERROR
rle_err_t     // RLE_E_NONE, RLE_E_PARAM, RLE_E_STATE, RLE_E_TRUNCATED, RLE_E_SPACE

rle8_error(&enc);           // returns rle_err_t
rle_strerror(err);          // human-readable string
```

## Example

See [example_rle8.c](example_rle8.c) for a minimal byte-by-byte encode loop.

```sh
gcc -Wall -o example_rle8 example_rle8.c rle8.c rle_common.c
./example_rle8
```

## Files

| File | Description |
|------|-------------|
| `rle_common.h` / `rle_common.c` | Shared types and `rle_strerror` |
| `rle8.h` / `rle8.c` | 8-bit RLE encoder/decoder |
| `rle16.h` / `rle16.c` | 16-bit RLE encoder/decoder |
| `rle_core.inc` | Shared implementation (included by rle8.c and rle16.c) |
| `example_rle8.c` | Usage example |
| `test_rle8.c`, `test_rle16.c`, `test_main.c` | Test suite |

## License

MIT — see [LICENSE](LICENSE).
