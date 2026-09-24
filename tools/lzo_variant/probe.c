/*
 * Which LZO variant produced the BF1942 archives?
 *
 * Measured 2026-09-24: the shipping archives and rfaPack.orig.exe's own -Compress output
 * carry byte-identical payloads, so one encoder made them all. It is not the miniLZO 2.10
 * lzo1x_1 we vendor: for 200 bytes of "ab" the era encoder emits 10 bytes and miniLZO 29,
 * and on the whole menu tree ours is 26% larger.
 *
 * The era encoder is also ~20x slower per thread than ours, which is the signature of a
 * high-compression variant rather than a cheap one, so this probe runs every candidate that
 * the full LZO library offers and prints what each produces for two inputs:
 *
 *   synthetic   200 bytes of "ab", where the era stream is known:
 *               13 61 62 20 a5 04 00 11 00 00
 *   a real file one entry of the vendor menu tree, whose stored payload can be read straight
 *               out of the shipping menu.rfa and compared byte for byte
 *
 * Usage: probe <real-file> <out-prefix>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lzo/lzo1x.h>

static unsigned char * load(const char * path, lzo_uint * size)
{
    FILE * fh = fopen(path, "rb");

    if (!fh) { perror(path); exit(2); }

    fseek(fh, 0, SEEK_END);
    long length = ftell(fh);
    fseek(fh, 0, SEEK_SET);

    unsigned char * data = malloc((size_t) length + 1);

    if (fread(data, 1, (size_t) length, fh) != (size_t) length) { fputs("short read\n", stderr); exit(2); }

    fclose(fh);
    *size = (lzo_uint) length;

    return data;
}

static void emit(const char * label, int rc, const unsigned char * out, lzo_uint out_len,
                 const unsigned char * in, lzo_uint in_len, void * work, const char * prefix)
{
    printf("  %-28s rc=%d  in=%u  out=%u", label, rc, (unsigned) in_len, (unsigned) out_len);

    if (out_len <= 32) {
        printf("   ");
        for (lzo_uint i = 0; i < out_len; ++i) printf("%02x ", out[i]);
    }

    printf("\n");

    /* round trip immediately: a variant that cannot be read back is not a candidate */
    if (rc == LZO_E_OK) {
        unsigned char * back = malloc(65536);
        lzo_uint back_len = 65536;
        int back_rc = lzo1x_decompress_safe(out, out_len, back, &back_len, NULL);

        if (back_rc != LZO_E_OK || back_len != in_len || memcmp(back, in, in_len) != 0) {
            printf("  %-28s ROUND TRIP FAILED (rc=%d, %u bytes)\n", "", back_rc, (unsigned) back_len);
        }

        if (prefix) {
            char path[512];
            snprintf(path, sizeof path, "%s.%s.lzo", prefix, label);

            for (char * p = path; *p; ++p) { if (*p == ' ') *p = '_'; }

            FILE * fh = fopen(path, "wb");
            if (fh) { fwrite(out, 1, out_len, fh); fclose(fh); }
        }

        free(back);
    }
}

static unsigned char * workmem;

static void run_all(const unsigned char * in, lzo_uint in_len, const char * prefix)
{
    static unsigned char out[1 << 20];
    lzo_uint out_len;
    int rc;

    /* Assign rc and out_len to locals before passing them on: argument evaluation order is
     * unspecified, so an earlier version of this probe read out_len before the call and
     * reported the buffer size instead of the result. */
    out_len = sizeof out;
    rc = lzo1x_1_compress(in, in_len, out, &out_len, workmem);
    emit("lzo1x_1", rc, out, out_len, in, in_len, workmem, prefix);

    out_len = sizeof out;
    rc = lzo1x_1_15_compress(in, in_len, out, &out_len, workmem);
    emit("lzo1x_1_15", rc, out, out_len, in, in_len, workmem, prefix);

    out_len = sizeof out;
    rc = lzo1x_999_compress(in, in_len, out, &out_len, workmem);
    emit("lzo1x_999", rc, out, out_len, in, in_len, workmem, prefix);

    for (int level = 1; level <= 9; ++level) {
        char label[64];
        snprintf(label, sizeof label, "lzo1x_999_l%d", level);

        out_len = sizeof out;
        rc = lzo1x_999_compress_level(in, in_len, out, &out_len, workmem, NULL, 0, NULL, level);
        emit(label, rc, out, out_len, in, in_len, workmem, prefix);
    }
}

int main(int argc, char ** argv)
{
    if (lzo_init() != LZO_E_OK) { fputs("lzo_init failed\n", stderr); return 1; }

    workmem = malloc(LZO1X_999_MEM_COMPRESS + 4096);

    if (!workmem) { fputs("no workmem\n", stderr); return 1; }

    unsigned char synthetic[200];
    for (int i = 0; i < 200; ++i) synthetic[i] = (i % 2) ? 'b' : 'a';

    printf("synthetic 200 x \"ab\"   (era encoder: 13 61 62 20 a5 04 00 11 00 00, 10 bytes)\n");
    run_all(synthetic, 200, NULL);
    printf("\n");

    if (argc >= 3) {
        lzo_uint size = 0;
        unsigned char * data = load(argv[1], &size);

        printf("%s  (%u bytes)\n", argv[1], (unsigned) size);
        run_all(data, size, argv[2]);
        free(data);
    }

    free(workmem);
    return 0;
}
