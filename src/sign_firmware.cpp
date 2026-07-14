/*
 * sign_firmware.cpp - Prepend an authorization/integrity header to a firmware
 * .bin file before uploading it to the Pico's /firmware_upload.cgi endpoint.
 *
 * Header layout (16 bytes, little-endian, defined in firmware_common.h):
 *     uint32_t magic;         // kFirmwareHeaderMagic
 *     uint32_t version;       // kFirmwareHeaderVersion, currently 1
 *     uint32_t payload_size;  // size of the firmware image that follows
 *     uint32_t payload_crc32; // CRC-32 (zlib / ISO-HDLC) of the firmware image
 *
 * Build (host compiler):
 *   g++ -O2 -std=c++17 -I../inc -o sign_firmware sign_firmware.cpp
 * Usage:
 *   ./sign_firmware app.bin app_signed.bin
 */
#include "firmware_common.h"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <array>

static void write_le32(FILE *f, uint32_t v) {
    std::array<uint8_t, 4> b = {
        static_cast<uint8_t>(v & 0xFF),
        static_cast<uint8_t>((v >> 8) & 0xFF),
        static_cast<uint8_t>((v >> 16) & 0xFF),
        static_cast<uint8_t>((v >> 24) & 0xFF),
    };
    fwrite(b.data(), 1, b.size(), f);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input.bin> <output.bin>\n", argv[0]);
        return 1;
    }

    const char *in_path  = argv[1];
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

    size_t size = static_cast<size_t>(size_l);
    std::vector<uint8_t> payload(size);

    if (fread(payload.data(), 1, size, in) != size) {
        fprintf(stderr, "error: failed to read input file\n");
        fclose(in);
        return 1;
    }
    fclose(in);

    uint32_t crc = crc32_update(0x00000000u, payload.data(), size);

    FILE *out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "error: cannot open output file '%s'\n", out_path);
        return 1;
    }

    write_le32(out, kFirmwareHeaderMagic);
    write_le32(out, kFirmwareHeaderVersion);
    write_le32(out, static_cast<uint32_t>(size));
    write_le32(out, crc);
    fwrite(payload.data(), 1, size, out);
    fclose(out);

    printf("Signed firmware written to: %s\n", out_path);
    printf("  payload size : %zu bytes\n", size);
    printf("  payload crc32: 0x%08x\n", crc);
    printf("  total size   : %zu bytes (header %zu + payload %zu)\n",
           size + sizeof(firmware_header), sizeof(firmware_header), size);

    return 0;
}
