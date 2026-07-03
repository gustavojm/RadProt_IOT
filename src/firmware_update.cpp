#include "firmware_update.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cstdint>

#include "FreeRTOS.h"
#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "pico/flash.h"
#include "pico/stdlib.h"
#include "portmacro.h"

#include "debug.h"
#include "settings.h"
#include "status.h"
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

struct firmware_metadata {
    uint32_t magic = 0;
    uint32_t flags = 0;
    uint32_t size = 0;
    uint32_t crc = 0;
    char file_name[32] = {0};
    uint32_t reserved[(FLASH_SECTOR_SIZE - 16u - sizeof(file_name)) / sizeof(uint32_t)] = {};
};

static_assert(sizeof(firmware_metadata) == FLASH_SECTOR_SIZE, "Metadata sector size mismatch");

struct firmware_upload_state {
    bool active = false;
    bool authorized = false;
    bool size_ok = false;
    bool finalised = false;
    uint32_t content_len = 0;
    uint32_t received = 0;
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t staging_erase_len = 0;
    uint32_t next_progress_log = kProgressLogStep;
    uint32_t sector_offset = 0;
    uint32_t sector_fill = 0;
    bool extension_ok = false;
    alignas(uint32_t) uint8_t sector[FLASH_SECTOR_SIZE] = {};
    char file_name[32] = {0};
    char message[80] = {0};
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

struct install_args {
    uint32_t app_bytes = 0;
};

static void run_flash_operation(void (*fn)(void *), void *arg);

static uint32_t align_up(uint32_t value, uint32_t align) {
    return (value + (align - 1u)) & ~(align - 1u);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
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

    char provided[sizeof(((client_mode_settings *)nullptr)->password)] = {0};
    copy_query_value(uri, "password", provided, sizeof provided);
    return strcmp(provided, reinterpret_cast<const char *>(get_client_mode_settings()->password)) == 0;
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
    strncpy(g_state.message, msg, sizeof(g_state.message) - 1);
    g_state.message[sizeof(g_state.message) - 1] = 0;
}

static void __not_in_flash_func(erase_flash_range_impl)(void *arg) {
    const erase_args *a = static_cast<const erase_args *>(arg);
    flash_range_erase(a->offset, a->length);
}

static void __not_in_flash_func(program_page_impl)(void *arg) {
    const page_write_args *a = static_cast<const page_write_args *>(arg);
    flash_range_program(a->offset, g_state.sector, a->length);
}

static void __not_in_flash_func(write_metadata_impl)(void *arg) {
    LWIP_UNUSED_ARG(arg);
    flash_range_erase(kMetadataOffset, FLASH_SECTOR_SIZE);
    flash_range_program(kMetadataOffset, g_state.sector, FLASH_SECTOR_SIZE);
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
    g_state.crc = 0xFFFFFFFFu;
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
    g_state.size_ok = (g_state.content_len > 0u) && (g_state.content_len <= kStagingSize);
    file_name_from_uri(uri, g_state.file_name, sizeof(g_state.file_name));
    g_state.extension_ok = has_bin_extension(g_state.file_name);
    g_state.active = g_state.authorized && g_state.size_ok && g_state.extension_ok;

    if (!g_state.authorized) {
        set_status_message("Wrong Password");
        return true;
    }

    if (!g_state.size_ok) {
        set_status_message("Firmware image too large");
        return true;
    }

    if (!g_state.extension_ok) {
        set_status_message("File must have a .bin extension");
        return true;
    }

    g_state.staging_erase_len = align_up(g_state.content_len, FLASH_SECTOR_SIZE);

    lDebug(Info, "Firmware upload accepted: %u bytes, staging %u bytes", g_state.content_len, g_state.staging_erase_len);

    erase_args erase = {
        .offset = kStagingOffset,
        .length = g_state.staging_erase_len,
    };
    lDebug(Info, "Erasing staging area at 0x%08x", kStagingOffset);
    run_flash_operation(erase_flash_range_impl, &erase);

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

        while (len > 0) {
            const size_t space = FLASH_SECTOR_SIZE - g_state.sector_fill;
            const size_t remaining = g_state.content_len - g_state.received;
            const size_t copy_len = (len < space) ? ((len < remaining) ? len : remaining) : ((space < remaining) ? space : remaining);

            if (copy_len == 0) {
                g_state.active = false;
                set_status_message("Unexpected upload length");
                return ERR_VAL;
            }

            if (g_state.sector_fill == 0) {
                g_state.sector_offset = kStagingOffset + g_state.received;
            }

            memcpy(&g_state.sector[g_state.sector_fill], src, copy_len);

            g_state.crc = crc32_update(g_state.crc, src, copy_len);
            g_state.sector_fill += copy_len;
            g_state.received += copy_len;
            src += copy_len;
            len -= copy_len;

            if (g_state.received >= g_state.next_progress_log || g_state.received == g_state.content_len) {
                lDebug(Info, "Receiving firmware: %u/%u bytes", g_state.received, g_state.content_len);
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

    if (g_state.received != g_state.content_len) {
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

    firmware_metadata *meta = reinterpret_cast<firmware_metadata *>(g_state.sector);
    *meta = {};
    meta->magic = kMetadataMagic;
    meta->flags = kPendingFlag;
    meta->size = g_state.received;
    meta->crc = g_state.crc;
    strncpy(meta->file_name, g_state.file_name, sizeof(meta->file_name) - 1);
    meta->file_name[sizeof(meta->file_name) - 1] = 0;

    lDebug(Info, "Staging complete: size=%u crc=0x%08x file name=%s", meta->size, meta->crc, meta->file_name);
    lDebug(Info, "Verifying staged firmware before install");
    run_flash_operation(write_metadata_impl, nullptr);

    g_state.finalised = true;
    g_state.active = false;
    g_state.crc = meta->crc;
    set_status_message("Upload staged");
}

ArduinoJson::MyJsonDocument firmware_upload_status_json() {
    ArduinoJson::MyJsonDocument json;

    if (!g_state.authorized) {
        json["message"] = g_state.message[0] ? g_state.message : "Wrong Password";
        return json;
    }

    if (!g_state.size_ok) {
        json["message"] = g_state.message[0] ? g_state.message : "Firmware image too large";
        return json;
    }

    if (!g_state.extension_ok) {
        json["message"] = g_state.message[0] ? g_state.message : "File must have a .bin extension";
        return json;
    }

    if (g_state.finalised) {
        json["OK"] = "Upload staged";
        json["size"] = g_state.received;
        json["crc"] = g_state.crc;
        json["file_name"] = g_state.file_name;
        json["reboot_required"] = true;
        return json;
    }

    json["message"] = g_state.message[0] ? g_state.message : "Upload not completed";
    return json;
}

void firmware_install_if_pending() {
    firmware_metadata *meta = reinterpret_cast<firmware_metadata *>(g_state.sector);
    *meta = *metadata_ptr();
    if (!has_pending_metadata(*meta)) {
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
    uint32_t crc = 0xFFFFFFFFu;
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
