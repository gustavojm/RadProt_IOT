/*
 * sign_firmware.c - Prepend an authorization/integrity header to a firmware
 * .bin file before uploading it to the Pico's /firmware_upload.cgi endpoint.
 *
 * Header layout (16 bytes, little-endian, must match firmware_update.cc):
 *     uint32_t magic;         // 0x50554657 - must match kFirmwareHeaderMagic
 *     uint32_t version;       // header format version, currently 1
 *     uint32_t payload_size;  // size of the firmware image that follows
 *     uint32_t payload_crc32; // CRC-32 (zlib / ISO-HDLC) of the firmware image
 *
 * The crc32_update() function below is copied verbatim from firmware_update.cc
 * so the CRC computed here is guaranteed bit-for-bit identical to what the
 * device computes while receiving the payload.
 *
 * Build:
 *   gcc -O2 -o sign_firmware sign_firmware.c
 * Usage:
 *   ./sign_firmware app.bin app_signed.bin
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define FW_MAGIC 0x50554657u /* must match kFirmwareHeaderMagic in firmware_update.cc */
#define FW_VERSION 1u

/* Identical to crc32_update() in firmware_update.cc (standard zlib/ISO-HDLC CRC-32). */
static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

/* Write a uint32_t to a file in little-endian order, regardless of host endianness. */
static void write_le32(FILE *f, uint32_t v) {
    uint8_t b[4] = {
        (uint8_t)(v & 0xFF),
        (uint8_t)((v >> 8) & 0xFF),
        (uint8_t)((v >> 16) & 0xFF),
        (uint8_t)((v >> 24) & 0xFF),
    };
    fwrite(b, 1, sizeof(b), f);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input.bin> <output.bin>\n", argv[0]);
        return 1;
    }

    const char *in_path = argv[1];
    const char *out_path = argv[2];

    FILE *in = fopen(in_path, "rb");
    if (!in) {
        fprintf(stderr, "error: cannot open input file '%s'\n", in_path);
        return 1;
    }

    if (fseek(in, 0, SEEK_END) != 0) {
        fprintf(stderr, "error: cannot seek input file\n");
        fclose(in);
        return 1;
    }
    long size_l = ftell(in);
    if (size_l <= 0) {
        fprintf(stderr, "error: input firmware file is empty\n");
        fclose(in);
        return 1;
    }
    rewind(in);

    size_t size = (size_t)size_l;
    uint8_t *payload = malloc(size);
    if (!payload) {
        fprintf(stderr, "error: out of memory (%zu bytes)\n", size);
        fclose(in);
        return 1;
    }

    if (fread(payload, 1, size, in) != size) {
        fprintf(stderr, "error: failed to read input file\n");
        fclose(in);
        free(payload);
        return 1;
    }
    fclose(in);

    /* Seed 0, matching reset_state() in firmware_update.cc: this makes the
     * result the standard CRC-32 (same as zlib/gzip/`cksum -a crc32`). */
    uint32_t crc = crc32_update(0x00000000u, payload, size);

    FILE *out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "error: cannot open output file '%s'\n", out_path);
        free(payload);
        return 1;
    }

    write_le32(out, FW_MAGIC);
    write_le32(out, FW_VERSION);
    write_le32(out, (uint32_t)size);
    write_le32(out, crc);
    fwrite(payload, 1, size, out);
    fclose(out);
    free(payload);

    printf("Signed firmware written to: %s\n", out_path);
    printf("  payload size : %zu bytes\n", size);
    printf("  payload crc32: 0x%08x\n", crc);
    printf("  total size   : %zu bytes (header 16 + payload %zu)\n", size + 16, size);

    return 0;
}
