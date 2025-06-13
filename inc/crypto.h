#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    uint32_t h[5];
    uint64_t len;
    uint8_t buf[64];
    uint32_t buf_len;
} sha1_ctx_t;

const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void sha1_transform(uint32_t state[5], const uint8_t block[64]);

void sha1_init(sha1_ctx_t *ctx);

void sha1_update(sha1_ctx_t *ctx, const uint8_t *data, size_t len);

void sha1_final(sha1_ctx_t *ctx, uint8_t hash[20]);

void base64_encode(const uint8_t *in, size_t in_len, char *out);
