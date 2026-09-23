# miniLZO (vendored)

LZO1X implementation used for RFA payload compression.

## Provenance

| | |
|---|---|
| Package | `minilzo-2.10` |
| Source | https://www.oberhumer.com/opensource/lzo/download/minilzo-2.10.tar.gz |
| Retrieved | 2026-09-23 |
| License | GPL-2.0-or-later — see `COPYING` |

Vendored **unmodified**: `minilzo.c`, `minilzo.h`, `lzoconf.h`, `lzodefs.h`,
plus the upstream `COPYING` and `README.LZO`. Do not edit these files; if an
upstream fix is needed, re-vendor a newer release instead.

## Why here and not under `cpputils/`

`cpputils/` is a **git submodule**. Writing our dependency inside it would dirty the
submodule and could not be committed from this repository. Vendored third-party code
belongs in `third_party/`.

## Why LZO at all

BF1942 `.rfa` payloads are LZO1X streams, compressed independently per 32 KiB chunk.
That is not guesswork: the working open-source RFA tool `yann-papouin/bga` bundles
miniLZO for exactly this, and we reproduced the LZO1X literal-run encoding byte for
byte against the original `rfaPack.exe -Compress`. See
`.github/skills/bf1942-standalone-map/references/rfa-format.md`.

LZO1X is stateless apart from a caller-supplied work buffer
(`LZO1X_1_MEM_COMPRESS` bytes, ~256 KB), and chunks are independent of one another —
so one buffer per thread gives lock-free, deterministic parallelism. That is what makes
the multi-core requirement tractable.

## Include layout

The four files must stay in **one flat directory**. `lzoconf.h` contains:

```c
#ifndef __LZODEFS_H_INCLUDED
#include <lzo/lzodefs.h>
#endif
```

which would need an `lzo/` subdirectory — but `minilzo.h` includes `"lzodefs.h"`
first, so `__LZODEFS_H_INCLUDED` is already defined and the prefixed include is
skipped. Always include `minilzo.h`, never `lzoconf.h` on its own.

## Build note

The project's global `AM_CPPFLAGS` contains `-std=c++20`, which is not valid for a C
translation unit. `libminilzo` therefore sets its own `CPPFLAGS` in `Makefile.am`,
replacing `AM_CPPFLAGS` for that target.

## Usage

```c
#include "minilzo.h"

if( lzo_init() != LZO_E_OK ) { /* ... */ }

std::vector<lzo_byte> wrkmem( LZO1X_1_MEM_COMPRESS );  // one per thread
lzo_uint compressed_len = /* output capacity */;
lzo1x_1_compress( src, src_len, dst, &compressed_len, wrkmem.data() );

lzo_uint out_len = /* expected size */;
lzo1x_decompress_safe( compressed, compressed_len, out, &out_len, nullptr );
```

Compression can legitimately produce **more** bytes than the input (incompressible
data). Callers must be able to fall back to storing the chunk verbatim — the RFA
format allows that, and real archives do it.
