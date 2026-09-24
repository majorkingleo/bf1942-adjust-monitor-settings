# Which LZO variant do the BF1942 archives use? — **LZO1X-999, level 8**

Answer, measured 2026-09-24: **LZO1X-999 at compression level 8**, applied to independent
32 KiB chunks, exactly as the container requires. The `lzo1x_1` inside the miniLZO we vendor is
a different, weaker variant — which is why our `-Compress` output was 8–26% larger than the
era tool's and why the shipped `rfaUnpack.exe` mis-decodes it.

## How it was established

1. `bin\rfaPack.orig.exe -Compress` reproduces the *shipping* archives' payloads byte for byte
   (618/618 entries of `menu.rfa`, 251/251 of the FH Pavlov archive), so one encoder produced
   all of them and it is deterministic. The sizes matching exactly was the first hint.
2. It is not `lzo1x_1`: for 200 bytes of `ab` the era encoder emits **10 bytes**
   (`13 61 62 20 a5 04 00 11 00 00`) where miniLZO emits **29**. For input that repetitive,
   that gap separates a strong match finder from a cheap one.
3. The full LZO 2.10 library carries the other variants. `probe.c` runs all of them on that
   synthetic input; **only `lzo1x_999` produces the era's 10 bytes** (levels 1–9 all do, since
   the input is trivial).
4. `chunked.c` then reproduces the archive's layout — 32 KiB chunks, concatenated — and its
   **level-8** output for `menu/AirControlsPage1` is **SHA-256 identical** to the payload
   stored in the shipping archive (`90D18D34…`, 4 886 bytes). Six further entries agree,
   from 60 B to 20 049 B, single- and multi-chunk:

   | entry | payload | level 8 |
   |---|---|---|
   | `menu/BackgroundLayer` | 60 B | identical |
   | `menu/CampaignMenu` | 3 966 B | identical |
   | `menu/AirControlsPage2` | 3 097 B | identical |
   | `menu/Texture/ingame_weaponbar_512x64.dds` | 1 005 B | identical |
   | `menu/Texture/Menu/buttons/menu_rubr_pil_16x8.dds` | 87 B | identical |
   | `menu/InGame` | 20 049 B | identical |

   Level 9 comes out two bytes smaller and level 7 larger on the largest of them, so the level
   really is 8 rather than "any 999".

## Reproducing

```sh
curl -O https://www.oberhumer.com/opensource/lzo/download/lzo-2.10.tar.gz
tar xzf lzo-2.10.tar.gz && cd lzo-2.10 && ./configure --disable-shared && make

gcc -O2 -I lzo-2.10/include -o probe.exe   probe.c   lzo-2.10/src/.libs/liblzo2.a
gcc -O2 -I lzo-2.10/include -o chunked.exe chunked.c lzo-2.10/src/.libs/liblzo2.a

./probe.exe                  # synthetic input: which variant reproduces the era stream
./chunked.exe <file> out     # writes out.l<level>, prints every chunk's compressed size

python dump_payload.py <archive.rfa> <entry name> payload.bin
```

Both C programs are self-contained apart from `lzo/lzo1x.h`. `probe.c` also round-trips every
variant it tries, because a variant that cannot be read back is not a candidate.

## Vendoring footprint

Linking only `lzo1x_999_compress` and `lzo1x_decompress_safe` pulls in **five** objects
(measured with `-Wl,-Map`):

```
lzo_init.o  lzo1x_1.o  lzo1x_1o.o  lzo1x_9x.o  lzo1x_d2.o
```

Decompression can stay with the miniLZO we already vendor and test, which makes a
compression-only vendoring the smaller change: `lzo1x_9x.c` and whatever it needs, plus the
public headers. LZO is GPLv2-or-later, the same licence family already accepted for miniLZO.

## What this changes

| | before (`lzo1x_1`) | with `lzo1x_999` level 8 |
|---|---|---|
| `menu.rfa` compress pack | 10 064 159 B | should be 7 963 020 B, i.e. the shipping size |
| readable by `rfaUnpack.orig.exe` | no (mis-decodes) | yes — the streams are the ones it wrote itself |
| compress mode byte-identical to the oracle | no (finding 17) | expected yes |

Costs: 999 is far slower per byte than 1x, and its per-thread work memory is
`LZO1X_999_MEM_COMPRESS` (~448 KB) instead of ~16 KB. Multi-threading still leaves us ahead of
the single-threaded 2003 encoder, but by a much smaller margin than the 21x measured for
`lzo1x_1`.
