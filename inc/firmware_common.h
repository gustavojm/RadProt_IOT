#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

// ---------------------------------------------------------------------------
// Firmware header prepended to every signed image by sign_firmware.
// Host-side and device-side code both include this header so the layout,
// magic value and version are defined in exactly one place.
// ---------------------------------------------------------------------------

struct firmware_header {
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t payload_size = 0;
    uint32_t payload_crc32 = 0;
};

static_assert(sizeof(firmware_header) == 16u, "Unexpected firmware header size");

constexpr uint32_t kFirmwareHeaderMagic   = 0x50554657u; // "WUPR"
constexpr uint32_t kFirmwareHeaderVersion = 1u;

// ---------------------------------------------------------------------------
// CRC-32 (zlib / ISO-HDLC).  Identical algorithm on host and device.
// Seed with 0 to produce the standard CRC-32 that tools like
// `cksum -a crc32`, zlib and gzip report.
// ---------------------------------------------------------------------------
inline uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

// ---------------------------------------------------------------------------
// Settings backup sector — stored at the last 4 KB sector of the staging
// area.  Survives both the staging-area erase during upload (which only
// covers align_up(payload_size, FLASH_SECTOR_SIZE) from kStagingOffset,
// reaching at most 0x1F0000) and the metadata-sector erase during install.
// ---------------------------------------------------------------------------

constexpr uint32_t kSettingsBackupMagic  = 0x53455452u; // "SETR"
constexpr uint32_t kSettingsBackupOffset = 0x1FE000u;

struct settings_backup_header {
    uint32_t magic;
    uint32_t json_len;  // byte count of JSON string, excluding null terminator
    uint32_t crc;       // CRC-32 of the JSON bytes (not including null)
};

static_assert(sizeof(settings_backup_header) == 12u, "Unexpected settings backup header size");
