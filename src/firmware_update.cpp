#include "firmware_update.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <new>

#include "FreeRTOS.h"
#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "pico/flash.h"
#include "pico/stdlib.h"
#include "portmacro.h"

#include "debug.h"
#include "firmware_common.h"
#include "post.h"
#include "settings.h"
#include "status.h"
#include "str_utils.h"
#include "watchdog.h"

namespace {

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2u * 1024u * 1024u)
#endif

constexpr uint32_t kFlashSize = PICO_FLASH_SIZE_BYTES;
constexpr uint32_t kAppSlotOffset = 0u;
constexpr uint32_t kAppSlotSize = 1024u * 1024u;
constexpr uint32_t kStagingOffset = kAppSlotSize;
constexpr uint32_t kMetadataOffset = kFlashSize - FLASH_SECTOR_SIZE;
constexpr uint32_t kStagingSize = kMetadataOffset - kStagingOffset;
constexpr uint32_t kProgressLogStep = 64u * 1024u;
constexpr uint32_t kPendingFlag = 0x00000001u;
constexpr uint32_t kMetadataMagic = 0x52445055u; // "RDPU"
static_assert(kAppSlotSize == (1024u * 1024u), "Unexpected app slot size");
static_assert(kStagingOffset < kMetadataOffset, "Flash layout overlap");

// firmware_header, kFirmwareHeaderMagic, kFirmwareHeaderVersion, and
// crc32_update() are defined in firmware_common.h (shared with sign_firmware).

struct firmware_metadata {
    uint32_t magic = 0;
    uint32_t flags = 0;
    uint32_t size = 0;
    uint32_t crc = 0;
    uint32_t source_magic = 0; // firmware_header.magic recorded at staging time,
                                // re-checked before install as a belt-and-suspenders
                                // guard against metadata-sector corruption.
    char file_name[32] = {0};
    uint32_t reserved[(FLASH_SECTOR_SIZE - 20u - sizeof(file_name)) / sizeof(uint32_t)] = {};
};

static_assert(sizeof(firmware_metadata) == FLASH_SECTOR_SIZE, "Metadata sector size mismatch");

struct firmware_upload_state {
    bool active = false;
    bool authorized = false;
    bool size_ok = false;
    bool finalised = false;
    bool preserve_settings = false;
    uint32_t content_len = 0;   // total HTTP body length: header + payload
    uint32_t received = 0;      // total bytes received so far (header + payload)
    uint32_t crc = 0xFFFFFFFFu; // running CRC over payload bytes only
    uint32_t staging_erase_len = 0;
    uint32_t next_progress_log = kProgressLogStep;
    uint32_t sector_offset = 0;
    uint32_t sector_fill = 0;
    bool extension_ok = false;
    alignas(uint32_t) uint8_t sector[FLASH_SECTOR_SIZE] = {};
    char file_name[32] = {0};
    char message[80] = {0};

    // Header parsing state.
    bool header_parsed = false;
    uint32_t header_received = 0; // bytes of the header collected so far
    firmware_header header = {};
    uint32_t payload_received = 0; // payload bytes received (excludes header)
};

static firmware_upload_state g_state;

struct page_write_args {
    uint32_t offset = 0;
    uint32_t length = 0;
};

struct erase_args {
    uint32_t offset = 0;
    uint32_t length = 0;
};

struct flash_write_args {
    uint32_t offset = 0;
    const uint8_t *data = nullptr;
    uint32_t length = 0;
};

struct install_args {
    uint32_t app_bytes = 0;
};

static void run_flash_operation(void (*fn)(void *), void *arg);

static uint32_t align_up(uint32_t value, uint32_t align) {
    return (value + (align - 1u)) & ~(align - 1u);
}

// crc32_update() is provided by firmware_common.h

static void copy_query_value(const char *uri, const char *key, char *out, size_t out_len) {
    if (out_len == 0) {
        return;
    }
    out[0] = 0;

    const char *q = strchr(uri, '?');
    if (!q) {
        return;
    }
    ++q;

    const size_t key_len = strlen(key);
    while (*q) {
        const char *pair_end = strchr(q, '&');
        const char *eq = strchr(q, '=');
        if (eq && (!pair_end || eq < pair_end) && static_cast<size_t>(eq - q) == key_len && strncmp(q, key, key_len) == 0) {
            const char *src = eq + 1;
            size_t written = 0;
            while (*src && (!pair_end || src < pair_end) && written + 1 < out_len) {
                if (*src == '%' && (!pair_end || src + 2 < pair_end) && isxdigit(static_cast<unsigned char>(src[1])) &&
                    isxdigit(static_cast<unsigned char>(src[2]))) {
                    char hex[3] = { src[1], src[2], 0 };
                    out[written++] = static_cast<char>(strtol(hex, nullptr, 16));
                    src += 3;
                } else if (*src == '+') {
                    out[written++] = ' ';
                    ++src;
                } else {
                    out[written++] = *src++;
                }
            }
            out[written] = 0;
            return;
        }
        if (!pair_end) {
            break;
        }
        q = pair_end + 1;
    }
}

static bool password_matches(const char *uri) {
    if (uri == nullptr) {
        return false;
    }
    if (initial_config) {
        return true;
    }

    char provided[sizeof(client_mode_settings().password)] = {0};
    copy_query_value(uri, "password", provided, sizeof provided);
    return strcmp(provided, get_client_mode_settings()->password) == 0;
}

static bool file_name_from_uri(const char *uri, char *file_name, size_t file_name_len) {
    if (uri == nullptr || file_name_len == 0) {
        return false;
    }
    copy_query_value(uri, "file_name", file_name, file_name_len);
    return file_name[0] != 0;
}

static bool has_bin_extension(const char *name) {
    if (name == nullptr) return false;
    size_t len = strlen(name);
    if (len < 4) return false;
    return name[len - 4] == '.' &&
           name[len - 3] == 'b' &&
           name[len - 2] == 'i' &&
           name[len - 1] == 'n';
}

static void set_status_message(const char *msg) {
    safe_strncpy(g_state.message, msg, sizeof(g_state.message));
}

static void __not_in_flash_func(erase_flash_range_impl)(void *arg) {
    const erase_args *a = static_cast<const erase_args *>(arg);
    flash_range_erase(a->offset, a->length);
}

static void __not_in_flash_func(program_page_impl)(void *arg) {
    const page_write_args *a = static_cast<const page_write_args *>(arg);
    flash_range_program(a->offset, g_state.sector, a->length);
}

static void __not_in_flash_func(flash_write_impl)(void *arg) {
    const flash_write_args *a = static_cast<const flash_write_args *>(arg);
    flash_range_program(a->offset, a->data, a->length);
}

static void __not_in_flash_func(write_metadata_impl)(void *arg) {
    LWIP_UNUSED_ARG(arg);
    flash_range_erase(kMetadataOffset, FLASH_SECTOR_SIZE);
    flash_range_program(kMetadataOffset, g_state.sector, FLASH_SECTOR_SIZE);
}

static void __not_in_flash_func(write_backup_impl)(void *arg) {
    const flash_write_args *a = static_cast<const flash_write_args *>(arg);
    flash_range_erase(kSettingsBackupOffset, kSettingsBackupSize);
    flash_range_program(kSettingsBackupOffset, a->data, kSettingsBackupSize);
}

static void __not_in_flash_func(erase_backup_impl)(void *arg) {
    LWIP_UNUSED_ARG(arg);
    flash_range_erase(kSettingsBackupOffset, kSettingsBackupSize);
}

static void run_flash_operation(void (*fn)(void *), void *arg) {
    flash_safe_execute(fn, arg, UINT32_MAX);
}

static void __not_in_flash_func(apply_install_impl)(void *arg) {
    install_args *a = static_cast<install_args *>(arg);
    firmware_metadata &meta = *reinterpret_cast<firmware_metadata *>(g_state.sector);

    const uint32_t app_erase_len = align_up(a->app_bytes, FLASH_SECTOR_SIZE);
    for (uint32_t offset = 0; offset < app_erase_len; offset += FLASH_SECTOR_SIZE) {
        flash_range_erase(kAppSlotOffset + offset, FLASH_SECTOR_SIZE);
    }

    uint32_t remaining = meta.size;
    uint32_t src = kStagingOffset;
    uint32_t dst = kAppSlotOffset;

    while (remaining > 0) {
        const uint32_t copy_len = remaining >= FLASH_PAGE_SIZE ? FLASH_PAGE_SIZE : remaining;
        uint8_t page_buf[FLASH_PAGE_SIZE];
        for (uint32_t i = 0; i < copy_len; ++i) {
            page_buf[i] = *(reinterpret_cast<const uint8_t *>(XIP_BASE + src + i));
        }
        for (uint32_t i = copy_len; i < FLASH_PAGE_SIZE; ++i) {
            page_buf[i] = 0xFFu;
        }

        flash_range_program(dst, page_buf, FLASH_PAGE_SIZE);

        src += FLASH_PAGE_SIZE;
        dst += FLASH_PAGE_SIZE;
        remaining = (remaining > FLASH_PAGE_SIZE) ? (remaining - FLASH_PAGE_SIZE) : 0;
    }

    memset(g_state.sector, 0, FLASH_SECTOR_SIZE);
    flash_range_erase(kMetadataOffset, FLASH_SECTOR_SIZE);
    flash_range_program(kMetadataOffset, g_state.sector, FLASH_SECTOR_SIZE);
}

static const firmware_metadata *metadata_ptr() {
    return reinterpret_cast<const firmware_metadata *>(XIP_BASE + kMetadataOffset);
}

static bool has_pending_metadata(const firmware_metadata &metadata) {
    return (metadata.magic == kMetadataMagic) && ((metadata.flags & kPendingFlag) != 0u) && (metadata.size > 0u);
}

static void reset_state() {
    g_state = {};
    // Seeded at 0, not 0xFFFFFFFF: with crc32_update()'s invert/un-invert
    // pattern, a seed of 0 makes this compute the standard, widely-known
    // CRC-32 (the same one zlib/gzip/PNG/`cksum -a crc32` produce), so any
    // standard tool on the host can reproduce the value in the firmware
    // header. (A seed of 0xFFFFFFFF, as used previously, is internally
    // consistent but is a non-standard variant that off-the-shelf CRC-32
    // tools won't reproduce.)
    g_state.crc = 0x00000000u;
    g_state.next_progress_log = kProgressLogStep;
}

} // namespace

bool firmware_upload_is_request(const char *uri) {
    return uri != nullptr && strncmp(uri, "/firmware_upload.cgi", strlen("/firmware_upload.cgi")) == 0;
}

bool firmware_upload_begin(struct http_state *hs, const char *uri, int content_len, u8_t *post_auto_wnd) {
    LWIP_UNUSED_ARG(hs);
    if (post_auto_wnd != nullptr) {
        *post_auto_wnd = 0;
    }
    reset_state();

    g_state.content_len = content_len > 0 ? static_cast<uint32_t>(content_len) : 0u;
    g_state.authorized = password_matches(uri);

    // At this point we only know the *total* HTTP body length; the payload
    // length is only known once the firmware_header has been received and
    // parsed (see firmware_upload_receive). We can still sanity-check the
    // total against the header size and staging capacity up front.
    constexpr uint32_t kHeaderSize = sizeof(firmware_header);
    const bool length_plausible = (g_state.content_len > kHeaderSize) &&
                                   ((g_state.content_len - kHeaderSize) <= kStagingSize);
    g_state.size_ok = length_plausible;

    file_name_from_uri(uri, g_state.file_name, sizeof(g_state.file_name));
    g_state.extension_ok = has_bin_extension(g_state.file_name);

    char preserve_flag[2] = {};
    copy_query_value(uri, "preserve_settings", preserve_flag, sizeof preserve_flag);
    g_state.preserve_settings = (preserve_flag[0] == '1');

    g_state.active = g_state.authorized && g_state.size_ok && g_state.extension_ok;

    if (!g_state.authorized) {
        set_status_message("Wrong Password");
        return true;
    }

    if (!g_state.size_ok) {
        set_status_message("Firmware image too large or missing header");
        return true;
    }

    if (!g_state.extension_ok) {
        set_status_message("File must have a .bin extension");
        return true;
    }

    lDebug(Info, "Firmware upload accepted: %u bytes total, awaiting header", g_state.content_len);

    // NOTE: staging erase is deferred to firmware_upload_receive(), once the
    // header magic/size have been validated. That way a bogus or corrupted
    // upload never touches flash at all.
    g_state.message[0] = 0;
    return true;
}

err_t firmware_upload_receive(struct http_state *hs, struct pbuf *p, const char *uri) {
    LWIP_UNUSED_ARG(hs);
    LWIP_UNUSED_ARG(uri);

    if (!g_state.active || !g_state.authorized || !g_state.size_ok) {
        return ERR_OK;
    }

    for (struct pbuf *q = p; q != nullptr; q = q->next) {
        const uint8_t *src = reinterpret_cast<const uint8_t *>(q->payload);
        size_t len = q->len;

        // --- Step 1: collect and validate the firmware_header, if not done yet.
        if (!g_state.header_parsed) {
            uint8_t *header_bytes = reinterpret_cast<uint8_t *>(&g_state.header);
            while (len > 0 && g_state.header_received < sizeof(firmware_header)) {
                header_bytes[g_state.header_received++] = *src++;
                --len;
                ++g_state.received;
            }

            if (g_state.header_received < sizeof(firmware_header)) {
                // Header spans multiple pbufs; wait for the rest.
                continue;
            }

            g_state.header_parsed = true;

            const bool magic_ok = (g_state.header.magic == kFirmwareHeaderMagic) &&
                                   (g_state.header.version == kFirmwareHeaderVersion);

            if (!magic_ok) {
                g_state.active = false;
                set_status_message("Invalid firmware image (bad magic)");
                lDebug(Error, "Firmware upload rejected: bad magic/version (0x%08x / %u)",
                       g_state.header.magic, g_state.header.version);
                return ERR_OK;
            }

            const uint32_t expected_payload = g_state.content_len - sizeof(firmware_header);
            const bool size_ok = (g_state.header.payload_size == expected_payload) &&
                                  (g_state.header.payload_size > 0u) &&
                                  (g_state.header.payload_size <= kStagingSize);

            if (!size_ok) {
                g_state.active = false;
                set_status_message("Firmware header size mismatch");
                lDebug(Error, "Firmware upload rejected: header payload_size=%u expected=%u",
                       g_state.header.payload_size, expected_payload);
                return ERR_OK;
            }

            // Header is valid - now safe to erase the staging area.
            g_state.staging_erase_len = align_up(g_state.header.payload_size, FLASH_SECTOR_SIZE);
            erase_args erase = {
                .offset = kStagingOffset,
                .length = g_state.staging_erase_len,
            };
            lDebug(Info, "Header OK (payload=%u bytes, crc=0x%08x), erasing staging area at 0x%08x",
                   g_state.header.payload_size, g_state.header.payload_crc32, kStagingOffset);
            run_flash_operation(erase_flash_range_impl, &erase);
        }

        // --- Step 2: remaining bytes in this pbuf are payload.
        while (len > 0) {
            const size_t space = FLASH_SECTOR_SIZE - g_state.sector_fill;
            const size_t remaining = g_state.header.payload_size - g_state.payload_received;
            const size_t copy_len = (len < space) ? ((len < remaining) ? len : remaining) : ((space < remaining) ? space : remaining);

            if (copy_len == 0) {
                g_state.active = false;
                set_status_message("Unexpected upload length");
                return ERR_OK;
            }

            if (g_state.sector_fill == 0) {
                g_state.sector_offset = kStagingOffset + g_state.payload_received;
            }

            memcpy(&g_state.sector[g_state.sector_fill], src, copy_len);

            g_state.crc = crc32_update(g_state.crc, src, copy_len);
            g_state.sector_fill += copy_len;
            g_state.payload_received += copy_len;
            g_state.received += copy_len;
            src += copy_len;
            len -= copy_len;

            if (g_state.payload_received >= g_state.next_progress_log ||
                g_state.payload_received == g_state.header.payload_size) {
                lDebug(Info, "Receiving firmware: %u/%u bytes", g_state.payload_received, g_state.header.payload_size);
                g_state.next_progress_log += kProgressLogStep;
            }

            if (g_state.sector_fill == FLASH_SECTOR_SIZE) {
                page_write_args page = {};
                page.offset = g_state.sector_offset;
                page.length = FLASH_SECTOR_SIZE;
                run_flash_operation(program_page_impl, &page);
                g_state.sector_fill = 0;
            }
        }
    }

#if LWIP_HTTPD_POST_MANUAL_WND
    if (hs != nullptr) {
        if (hs->pcb != nullptr) {
            altcp_recved(hs->pcb, p->tot_len);
        }
    }
#endif

    return ERR_OK;
}

void firmware_upload_finish(struct http_state *hs, const char *uri) {
    LWIP_UNUSED_ARG(hs);
    LWIP_UNUSED_ARG(uri);

    if (!g_state.authorized) {
        return;
    }

    if (!g_state.size_ok) {
        return;
    }

    if (!g_state.extension_ok) {
        return;
    }

    if (!g_state.header_parsed) {
        g_state.active = false;
        set_status_message("Incomplete firmware header");
        return;
    }

    // If firmware_upload_receive already detected an error and deactivated
    // the upload, preserve its (more specific) status message.
    if (!g_state.active) {
        return;
    }

    if (g_state.received != g_state.content_len || g_state.payload_received != g_state.header.payload_size) {
        g_state.active = false;
        set_status_message("Unexpected upload length");
        return;
    }

    if (g_state.sector_fill > 0) {
        page_write_args page = {};
        page.offset = g_state.sector_offset;
        page.length = FLASH_SECTOR_SIZE;
        for (uint32_t i = g_state.sector_fill; i < FLASH_SECTOR_SIZE; ++i) {
            g_state.sector[i] = 0xFFu;
        }
        lDebug(Info, "Flushing final partial flash sector (%u bytes)", g_state.sector_fill);
        run_flash_operation(program_page_impl, &page);
        g_state.sector_fill = 0;
    }

    // Verify the payload we received matches the CRC the host computed from
    // the original file. This is the key robustness improvement: it catches
    // truncation/corruption/tampering in transit, not just flash-write errors.
    if (g_state.crc != g_state.header.payload_crc32) {
        g_state.active = false;
        set_status_message("Firmware CRC mismatch - upload corrupted");
        lDebug(Error, "CRC mismatch: header=0x%08x computed=0x%08x", g_state.header.payload_crc32, g_state.crc);
        return;
    }

    firmware_metadata *meta = reinterpret_cast<firmware_metadata *>(g_state.sector);
    *meta = {};
    meta->magic = kMetadataMagic;
    meta->flags = kPendingFlag;
    meta->size = g_state.payload_received;
    meta->crc = g_state.crc;
    meta->source_magic = g_state.header.magic;
    safe_strncpy(meta->file_name, g_state.file_name, sizeof(meta->file_name));

    lDebug(Info, "Staging complete: size=%u crc=0x%08x file name=%s", meta->size, meta->crc, meta->file_name);
    lDebug(Info, "Verifying staged firmware before install");
    run_flash_operation(write_metadata_impl, nullptr);

    if (g_state.preserve_settings) {
        auto backup_json = get_settings_backup_json();
        size_t json_len = ArduinoJson::measureJson(backup_json);
        constexpr size_t kMaxJsonLen = kSettingsBackupSize - sizeof(settings_backup_header);
        if (json_len > 0 && json_len <= kMaxJsonLen) {
            auto *buf = new(std::nothrow) alignas(uint32_t) uint8_t[kSettingsBackupSize];
            if (buf) {
                memset(buf, 0, kSettingsBackupSize);
                auto *hdr = reinterpret_cast<settings_backup_header *>(buf);
                hdr->magic = kSettingsBackupMagic;
                hdr->json_len = static_cast<uint32_t>(json_len);
                ArduinoJson::serializeJson(backup_json, buf + sizeof(settings_backup_header), kMaxJsonLen);
                hdr->crc = crc32_update(0, buf + sizeof(settings_backup_header), json_len);
                lDebug(Info, "Settings backup: %u bytes, writing to 0x%08x", static_cast<unsigned>(json_len), kSettingsBackupOffset);
                flash_write_args wargs = { .offset = kSettingsBackupOffset, .data = buf, .length = kSettingsBackupSize };
                run_flash_operation(write_backup_impl, &wargs);
                delete[] buf;
            } else {
                lDebug(Warn, "Settings backup: out of memory, skipping");
            }
        } else {
            lDebug(Warn, "Settings backup too large (%u bytes), skipping", static_cast<unsigned>(json_len));
        }
    }

    g_state.finalised = true;
    g_state.active = false;
    g_state.crc = meta->crc;
    set_status_message("Upload staged");
}

ArduinoJson::MyJsonDocument firmware_upload_status_json() {
    auto responseJson = ArduinoJson::MyJsonDocument();

    if (g_state.finalised) {
        responseJson["OK"] = "Upload staged";
        responseJson["size"] = g_state.received;
        responseJson["crc"] = g_state.crc;
        responseJson["file_name"] = g_state.file_name;
        responseJson["reboot_required"] = true;
    return responseJson;
    }

    responseJson["message"] = g_state.message[0] ? g_state.message : "Upload not completed";
    return responseJson;
}

void firmware_install_if_pending() {
    firmware_metadata *meta = reinterpret_cast<firmware_metadata *>(g_state.sector);
    *meta = *metadata_ptr();
    if (!has_pending_metadata(*meta)) {
        return;
    }

    // Defense in depth: re-check the header magic recorded at staging time.
    // This is independent of the upload-time check and guards against the
    // (unlikely but possible) case of bit-rot/corruption of the metadata
    // sector itself between staging and reboot.
    if (meta->source_magic != kFirmwareHeaderMagic) {
        lDebug(Error, "Staged firmware missing/invalid source magic (0x%08x), refusing to install",
               meta->source_magic);
        memset(g_state.sector, 0, FLASH_SECTOR_SIZE);
        run_flash_operation(write_metadata_impl, nullptr);
        return;
    }

    const uint32_t app_capacity = kAppSlotSize;
    if (meta->size > app_capacity) {
        lDebug(Error, "Staged firmware too large (%u > %u)", meta->size, app_capacity);
        memset(g_state.sector, 0, FLASH_SECTOR_SIZE);
        run_flash_operation(write_metadata_impl, nullptr);
        return;
    }

    const uint8_t *staged = reinterpret_cast<const uint8_t *>(XIP_BASE + kStagingOffset);
    uint32_t crc = 0x00000000u; // must match the seed used in reset_state()
    uint32_t remaining = meta->size;
    while (remaining > 0) {
        const uint32_t step = remaining > FLASH_PAGE_SIZE ? FLASH_PAGE_SIZE : remaining;
        crc = crc32_update(crc, staged + (meta->size - remaining), step);
        remaining -= step;
    }
    if (crc != meta->crc) {
        lDebug(Error, "Staged firmware CRC mismatch: expected %08x got %08x", meta->crc, crc);
        memset(g_state.sector, 0, FLASH_SECTOR_SIZE);
        run_flash_operation(write_metadata_impl, nullptr);
        return;
    }

    install_args args = {};
    args.app_bytes = align_up(meta->size, FLASH_SECTOR_SIZE);
    lDebug(Info, "Installing staged firmware to app slot");
    run_flash_operation(apply_install_impl, &args);

    lDebug(Info, "Firmware updated successfully, rebooting");
    vTaskSuspend(feedWdTask_handle);
    watchdog_reboot(0, SRAM_END, 100);
}

void restore_settings_if_pending() {
    const auto *base = reinterpret_cast<const uint8_t *>(XIP_BASE + kSettingsBackupOffset);
    const auto *hdr = reinterpret_cast<const settings_backup_header *>(base);

    if (hdr->magic != kSettingsBackupMagic) {
        return;
    }

    const char *json_str = reinterpret_cast<const char *>(base) + sizeof(settings_backup_header);
    uint32_t crc = crc32_update(0, reinterpret_cast<const uint8_t *>(json_str), hdr->json_len);
    if (crc != hdr->crc) {
        lDebug(Warn, "Settings backup CRC mismatch (expected 0x%08x, got 0x%08x), erasing", hdr->crc, crc);
        run_flash_operation(erase_backup_impl, nullptr);
        return;
    }

    lDebug(Info, "Restoring settings from backup (%u bytes)", hdr->json_len);

    ArduinoJson::MyJsonDocument doc;
    ArduinoJson::DeserializationError error = ArduinoJson::deserializeJson(doc, json_str, hdr->json_len);
    if (error) {
        lDebug(Error, "Settings backup JSON parse error: %s, erasing", error.c_str());
        run_flash_operation(erase_backup_impl, nullptr);
        return;
    }

    ArduinoJson::MyJsonDocument responseJson;
    if (apply_settings_from_json(doc, responseJson, true)) {
        lDebug(Info, "Settings restored from backup");
    } else {
        lDebug(Warn, "Settings restore failed: %s", responseJson["message"] | "unknown error");
    }

    run_flash_operation(erase_backup_impl, nullptr);
}
