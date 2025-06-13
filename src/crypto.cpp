#include "crypto.h"

void sha1_transform(uint32_t state[5], const uint8_t block[64]) {
    uint32_t w[80], a, b, c, d, e, f, k, temp;
    for (int i = 0; i < 16; i++) {
        w[i] = (block[i * 4] << 24) | (block[i * 4 + 1] << 16) |
               (block[i * 4 + 2] << 8) | (block[i * 4 + 3]);
    }
    for (int i = 16; i < 80; i++) {
        w[i] = (w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]);
        w[i] = (w[i] << 1) | (w[i] >> 31);
    }

    a = state[0]; b = state[1]; c = state[2];
    d = state[3]; e = state[4];

    for (int i = 0; i < 80; i++) {
        if (i < 20)      { f = (b & c) | ((~b) & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;            k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;            k = 0xCA62C1D6; }

        temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
        e = d; d = c;
        c = (b << 30) | (b >> 2);
        b = a; a = temp;
    }

    state[0] += a; state[1] += b; state[2] += c;
    state[3] += d; state[4] += e;
}

void sha1_init(sha1_ctx_t *ctx) {
    ctx->h[0] = 0x67452301;
    ctx->h[1] = 0xEFCDAB89;
    ctx->h[2] = 0x98BADCFE;
    ctx->h[3] = 0x10325476;
    ctx->h[4] = 0xC3D2E1F0;
    ctx->len = 0;
    ctx->buf_len = 0;
}

void sha1_update(sha1_ctx_t *ctx, const uint8_t *data, size_t len) {
    ctx->len += len * 8;
    while (len > 0) {
        size_t to_copy = 64 - ctx->buf_len;
        if (to_copy > len) to_copy = len;
        memcpy(ctx->buf + ctx->buf_len, data, to_copy);
        ctx->buf_len += to_copy;
        data += to_copy;
        len -= to_copy;

        if (ctx->buf_len == 64) {
            sha1_transform(ctx->h, ctx->buf);
            ctx->buf_len = 0;
        }
    }
}

void sha1_final(sha1_ctx_t *ctx, uint8_t hash[20]) {
    ctx->buf[ctx->buf_len++] = 0x80;
    if (ctx->buf_len > 56) {
        while (ctx->buf_len < 64) ctx->buf[ctx->buf_len++] = 0;
        sha1_transform(ctx->h, ctx->buf);
        ctx->buf_len = 0;
    }
    while (ctx->buf_len < 56) ctx->buf[ctx->buf_len++] = 0;
    for (int i = 7; i >= 0; i--)
        ctx->buf[ctx->buf_len++] = (ctx->len >> (i * 8)) & 0xFF;
    sha1_transform(ctx->h, ctx->buf);

    for (int i = 0; i < 5; i++) {
        hash[i * 4 + 0] = (ctx->h[i] >> 24) & 0xFF;
        hash[i * 4 + 1] = (ctx->h[i] >> 16) & 0xFF;
        hash[i * 4 + 2] = (ctx->h[i] >> 8) & 0xFF;
        hash[i * 4 + 3] = ctx->h[i] & 0xFF;
    }
}

void base64_encode(const uint8_t *in, size_t in_len, char *out) {
    size_t i, o = 0;
    for (i = 0; i + 2 < in_len; i += 3) {
        out[o++] = b64_table[in[i] >> 2];
        out[o++] = b64_table[((in[i] & 0x3) << 4) | (in[i + 1] >> 4)];
        out[o++] = b64_table[((in[i + 1] & 0xF) << 2) | (in[i + 2] >> 6)];
        out[o++] = b64_table[in[i + 2] & 0x3F];
    }
    if (i < in_len) {
        out[o++] = b64_table[in[i] >> 2];
        if (i + 1 < in_len) {
            out[o++] = b64_table[((in[i] & 0x3) << 4) | (in[i + 1] >> 4)];
            out[o++] = b64_table[(in[i + 1] & 0xF) << 2];
        } else {
            out[o++] = b64_table[(in[i] & 0x3) << 4];
            out[o++] = '=';
        }
        out[o++] = '=';
    }
    out[o] = '\0';
}
