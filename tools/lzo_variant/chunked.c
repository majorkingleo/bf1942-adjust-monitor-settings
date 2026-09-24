/*
 * Compress a file exactly the way an .rfa stores it: independent 32 KiB chunks, one
 * concatenated payload. Then the result can be compared byte for byte against the payload of
 * a shipping archive entry, which is the only way to prove which encoder wrote it.
 *
 * Usage: chunked <infile> <outprefix>
 *        writes <outprefix>.l<level> and prints every chunk's compressed size.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lzo/lzo1x.h>

#define CHUNK 32768

static const int LEVELS[] = { 1, 7, 8, 9 };

int main(int argc, char ** argv)
{
    if (argc < 3) { fputs("usage: chunked <infile> <outprefix>\n", stderr); return 2; }

    if (lzo_init() != LZO_E_OK) { fputs("lzo_init failed\n", stderr); return 1; }

    FILE * fh = fopen(argv[1], "rb");

    if (!fh) { perror(argv[1]); return 2; }

    fseek(fh, 0, SEEK_END);
    long length = ftell(fh);
    fseek(fh, 0, SEEK_SET);

    unsigned char * in = malloc((size_t) length + 1);

    if (fread(in, 1, (size_t) length, fh) != (size_t) length) { fputs("short read\n", stderr); return 2; }

    fclose(fh);

    unsigned char * workmem = malloc(LZO1X_999_MEM_COMPRESS + 4096);
    unsigned char * out = malloc((size_t) length * 2 + 4096);

    if (!workmem || !out) { fputs("no memory\n", stderr); return 1; }

    printf("%s  %ld bytes  %ld chunk(s)\n", argv[1], length,
           (long) ((length + CHUNK - 1) / CHUNK));

    for (size_t k = 0; k < sizeof(LEVELS) / sizeof(LEVELS[0]); ++k) {
        const int level = LEVELS[k];
        size_t total = 0;
        lzo_uint offset = 0;
        int ok = 1;

        for (long pos = 0; pos < length; pos += CHUNK) {
            const lzo_uint n = (lzo_uint) ((length - pos < CHUNK) ? (length - pos) : CHUNK);
            lzo_uint out_len = (lzo_uint) n + n / 16 + 64 + 3;
            int rc = lzo1x_999_compress_level(in + pos, n, out + total, &out_len,
                                              workmem, NULL, 0, NULL, level);

            if (rc != LZO_E_OK) { printf("  level %d: rc=%d\n", level, rc); ok = 0; break; }

            printf("  level %d  chunk %ld  %6u -> %5u\n", level, (long) (pos / CHUNK), (unsigned) n, (unsigned) out_len);

            total += out_len;
            offset = (lzo_uint) total;
        }

        if (!ok) continue;

        char path[512];
        snprintf(path, sizeof path, "%s.l%d", argv[2], level);

        FILE * ofh = fopen(path, "wb");

        if (ofh) { fwrite(out, 1, total, ofh); fclose(ofh); }

        printf("  level %d  total payload %u bytes -> %s\n", level, (unsigned) offset, path);
    }

    free(out);
    free(workmem);
    free(in);

    return 0;
}
