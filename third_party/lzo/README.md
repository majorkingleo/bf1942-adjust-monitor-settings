# LZO (vendored, partial)

| | |
|---|---|
| Package | LZO 2.10 |
| Source | `https://www.oberhumer.com/opensource/lzo/download/lzo-2.10.tar.gz` |
| Licence | GPLv2-or-later — see `COPYING` |
| Contents | 5 of upstream's sources plus their header closure, unmodified |

Verified by compiling `tools/lzo_variant/probe.c` against these files with the project's mingw
toolchain and reproducing the streams measured out of shipping archives.

## Why the full library rather than miniLZO

miniLZO implements LZO1X-1 only, and the BF1942 archives were **not** written with it. They use
**LZO1X-999 at level 8** (finding 31 in `PLAN_rfa_tools.md`): for 200 bytes of `ab` that encoder
emits 10 bytes where LZO1X-1 emits 29, and over the whole `menu` tree it is 21% smaller. LZO1X-1
is still available here as the `--lzo-fast` opt-in, so one library serves both variants.

## Why only five translation units

That is the closure the linker pulls in for `lzo1x_999_compress_level`, `lzo1x_1_compress` and
`lzo1x_decompress_safe`, measured with `-Wl,-Map`:

```
src/lzo_init.c  src/lzo1x_1.c  src/lzo1x_1o.c  src/lzo1x_9x.c  src/lzo1x_d2.c
```

The rest of the family — LZO1, LZO1A, LZO1B, LZO1C, LZO1F, LZO1Y, LZO1Z, LZO2A, the checksums,
the optimizer — is about forty further files plus a configure step and a `config.h`, and this
project has no use for any of it.

They compile standalone: no `config.h`, no autotools. Two consequences for the build:

* the sources include their internal headers by plain name (`"lzo_conf.h"`, `"config1x.h"`), so
  `third_party/lzo/src` has to be on the include path along with `third_party/lzo/include`;
* the target gets its own `CPPFLAGS`, because `AM_CPPFLAGS` carries `-std=c++20`, which is not a
  valid option for a C translation unit.

## Not compatible with miniLZO

`minilzo.h` refuses to compile alongside LZO ("you cannot use both LZO and miniLZO"), and the two
define the same symbols (`lzo1x_1_compress`, `lzo1x_decompress_safe`, `lzo_init`). So this
directory **replaced** `third_party/minilzo/` rather than joining it.
