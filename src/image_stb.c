/* Copyright Planimeter. All Rights Reserved. */

#include "image.h"
#include "filesystem.h"
#include "graphics.h"
#include <stdlib.h>
#include <stdio.h>

/* Use stb_image vendored in third_party. */
#define STB_IMAGE_IMPLEMENTATION
#include "stb-2.30/stb_image.h"

/* Read a file, preferring the real filesystem for absolute paths. PhysFS is
 * mounted at the executable directory, so model textures referenced by
 * absolute repo path are invisible to it. */
static unsigned char *read_image_file(const char *path, size_t *outSize)
{
    void *filedata = NULL;
    size_t filesize = filesystem_fileread(&filedata, path);
    if (filesize > 0 && filedata) {
        *outSize = filesize;
        return (unsigned char *)filedata;
    }

    /* Absolute path outside the PhysFS mount: read it directly. */
    if (path && path[0] == '/') {
        FILE *f = fopen(path, "rb");
        if (!f) {
            return NULL;
        }
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (len <= 0) {
            fclose(f);
            return NULL;
        }
        unsigned char *buf = (unsigned char *)malloc((size_t)len);
        if (!buf) {
            fclose(f);
            return NULL;
        }
        size_t got = fread(buf, 1, (size_t)len, f);
        fclose(f);
        if (got != (size_t)len) {
            free(buf);
            return NULL;
        }
        *outSize = (size_t)len;
        return buf;
    }

    return NULL;
}

Texture image_load(const char *path)
{
    if ( path == NULL ) {
        return NULL;
    }

    size_t filesize = 0;
    unsigned char *filedata = read_image_file(path, &filesize);
    if (filesize == 0 || filedata == NULL) {
        return NULL;
    }

    int width = 0, height = 0, channels = 0;
    unsigned char *pixels = stbi_load_from_memory(filedata,
                                                  (int)filesize,
                                                  &width,
                                                  &height,
                                                  &channels,
                                                  STBI_rgb_alpha);
    free(filedata);

    if (!pixels) {
        return NULL;
    }

    Texture tex = graphics_createtexture_rgba(width, height, pixels);
    stbi_image_free(pixels);
    return tex;
}

void image_free(Texture tex)
{
    if (!tex) {
        return;
    }
    graphics_destroytexture(tex);
}

/* ------------------------------------------------------------------ */
/*  PNG output for GAME_CAPTURE                                       */
/*                                                                    */
/*  stb_image_write is not vendored, and a capture only needs to be   */
/*  byte-comparable rather than small, so the encoder below emits     */
/*  valid zlib "stored" blocks: no compression, no dependency.        */
/* ------------------------------------------------------------------ */

#include <string.h>

static uint32_t png_crc_table[256];
static int png_crc_ready;

static void png_crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) {
            c = (c & 1u) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
        }
        png_crc_table[n] = c;
    }
    png_crc_ready = 1;
}

static uint32_t png_crc(const unsigned char *data, size_t len)
{
    if (!png_crc_ready) {
        png_crc_init();
    }
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < len; i++) {
        c = png_crc_table[(c ^ data[i]) & 0xffu] ^ (c >> 8);
    }
    return c ^ 0xffffffffu;
}

static void png_put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static int png_write_chunk(FILE *f, const char *type,
                           const unsigned char *data, size_t len)
{
    unsigned char header[8];
    png_put32(header, (uint32_t)len);
    memcpy(header + 4, type, 4);

    unsigned char crcBuf[4];
    uint32_t c = png_crc((const unsigned char *)type, 4);
    if (len > 0) {
        /* CRC covers the type and the data, but it is computed over one
         * contiguous run, so re-hash both parts. */
        unsigned char *tmp = (unsigned char *)malloc(4 + len);
        if (!tmp) return 0;
        memcpy(tmp, type, 4);
        memcpy(tmp + 4, data, len);
        c = png_crc(tmp, 4 + len);
        free(tmp);
    }
    png_put32(crcBuf, c);

    if (fwrite(header, 1, 8, f) != 8) return 0;
    if (len > 0 && fwrite(data, 1, len, f) != len) return 0;
    if (fwrite(crcBuf, 1, 4, f) != 4) return 0;
    return 1;
}

int image_write_png(const char *path, int width, int height,
                    const unsigned char *rgba)
{
    if (!path || !rgba || width <= 0 || height <= 0) {
        return 0;
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "image_write_png: cannot open %s\n", path);
        return 0;
    }

    static const unsigned char signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (fwrite(signature, 1, 8, f) != 8) {
        fclose(f);
        return 0;
    }

    unsigned char ihdr[13];
    png_put32(ihdr, (uint32_t)width);
    png_put32(ihdr + 4, (uint32_t)height);
    ihdr[8] = 8;    /* bit depth */
    ihdr[9] = 6;    /* colour type: truecolour with alpha */
    ihdr[10] = 0;   /* deflate */
    ihdr[11] = 0;   /* adaptive filtering */
    ihdr[12] = 0;   /* no interlace */
    if (!png_write_chunk(f, "IHDR", ihdr, sizeof(ihdr))) {
        fclose(f);
        return 0;
    }

    /* Raw scanlines, each prefixed with filter type 0 (None). */
    size_t stride = (size_t)width * 4;
    size_t rawSize = (stride + 1) * (size_t)height;
    unsigned char *raw = (unsigned char *)malloc(rawSize);
    if (!raw) {
        fclose(f);
        return 0;
    }
    for (int y = 0; y < height; y++) {
        unsigned char *row = raw + (size_t)y * (stride + 1);
        row[0] = 0;
        memcpy(row + 1, rgba + (size_t)y * stride, stride);
    }

    /* zlib stream: header, stored blocks of at most 65535 bytes, adler32. */
    size_t blocks = (rawSize + 65534) / 65535;
    size_t idatSize = 2 + rawSize + blocks * 5 + 4;
    unsigned char *idat = (unsigned char *)malloc(idatSize);
    if (!idat) {
        free(raw);
        fclose(f);
        return 0;
    }

    size_t o = 0;
    idat[o++] = 0x78;   /* CMF: deflate, 32K window */
    idat[o++] = 0x01;   /* FLG: no dictionary, fastest */

    size_t offset = 0;
    while (offset < rawSize) {
        size_t remaining = rawSize - offset;
        size_t chunk = remaining > 65535 ? 65535 : remaining;
        int last = (offset + chunk >= rawSize);

        idat[o++] = (unsigned char)(last ? 1 : 0);
        idat[o++] = (unsigned char)(chunk & 0xff);
        idat[o++] = (unsigned char)((chunk >> 8) & 0xff);
        idat[o++] = (unsigned char)(~chunk & 0xff);
        idat[o++] = (unsigned char)((~chunk >> 8) & 0xff);
        memcpy(idat + o, raw + offset, chunk);
        o += chunk;
        offset += chunk;
    }

    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < rawSize; i++) {
        a = (a + raw[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    uint32_t adler = (b << 16) | a;
    png_put32(idat + o, adler);
    o += 4;

    free(raw);

    int ok = png_write_chunk(f, "IDAT", idat, o);
    free(idat);
    if (!ok || !png_write_chunk(f, "IEND", NULL, 0)) {
        fclose(f);
        return 0;
    }

    fclose(f);
    return 1;
}
