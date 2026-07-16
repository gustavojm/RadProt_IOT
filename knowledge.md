# Project Knowledge

## Memory Safety

### Critical Rules (will cause crashes or silent corruption)

1. NEVER delete/free a buffer that `hs->file` points to in the HTTP server.
   The `http_state->file` pointer is used asynchronously by lwIP to send data.
   Ownership pattern: allocate via `new[]`, track via `http_state->post_response_buf`,
   and let `http_state_eof()` free it. See `httpd.h:243` and `httpd.cpp:325-331`.

2. lwIP heap (`mem_malloc`/`mem_free`) and FreeRTOS heap (`pvPortMalloc`/`vPortFree`)
   are SEPARATE heaps when `MEM_LIBC_MALLOC=0`. Never mix allocators.
   `new[]`/`delete[]` → FreeRTOS heap via `c_cpp_config.cpp` overrides.
   `mem_malloc`/`mem_free` → lwIP internal heap.

3. Every WebSocket frame handler must validate frame length BEFORE accessing
   extended header bytes (`msg[2]`, `msg[3]`, `msg[10]`). Don't just check `length < 2`.

### High-Risk Patterns

4. `snprintf` return value is the *would-be* length, not the *written* length.
   Always clamp before passing to write/send: `if (ret > bufsize) ret = bufsize - 1;`

5. `strcat` into a fixed buffer is forbidden. Always use bounded operations
   (`snprintf`, `memcpy` with explicit length checks).

6. String search results must be null-checked BEFORE pointer arithmetic:
   ```cpp
   // BAD:  char *p = strstr(buf, "X") + 6;  // crash if NULL
   // GOOD: char *p = strstr(buf, "X"); if (!p) return; p += 6;
   ```

7. Uninitialized local variables are UB. Initialize all variables on declaration,
   especially return values in error paths.

8. Static local variables in FreeRTOS task functions are shared across all
   concurrent executions of that function → reentrancy bug. Use stack locals.

### Embedded-Specific

9. Hardware busy-wait loops need timeouts. If a PHY or peripheral hangs,
   the loop must not spin forever.

10. Semaphore/mutex creation can fail under memory pressure.
    Always check the return value before using.

11. `new` (via `pvPortMalloc`) never returns NULL on FreeRTOS with `heap_4`
    (`configASSERT` fires on failure in debug configs). Null checks after `new`
    are dead code but harmless defensive practice.

12. Shared IRQ handler registration (`irq_add_shared_handler`) can succeed
    even if the IRQ was previously claimed. Check with `irq_get_exclusive_handler`
    first.

## Settings Backup/Restore

- **Backup offset**: `kSettingsBackupOffset = 0x1FE000` (last sector of staging area)
- **Preserved**: Only `s_Client_Settings` (WiFi/Ethernet/MQTT/sensors), NOT `s_Settings` (AP-mode)
- **Backup format**: JSON with `"encoded":true`, hex-encoded passwords (via `get_settings_backup_json()`)
- **Backup layout**: 12-byte header (`settings_backup_header`: magic 0x53455452, json_len, crc32) + JSON string
- **Backup sector survives install**: `apply_install_impl` erases app slot + metadata but NOT staging area
- **Boot flow**: Upload → reboot → install → reboot → `restore_settings_if_pending()` → normal init (2 reboots total)
- **Restore**: Runs in main_task after install, calls `write_client_mode_settings()` if backup is valid

## Password Protection

- **`auth_password`**: The authentication mechanism across all endpoints (save, backup, restore)
- **`settings.password`** in JSON: Part of the settings data, NOT for authentication
- **`verify_settings_password()`**: Checks `auth_password` first (plaintext); falls back to `settings.password` (encoded/non-encoded)
- **`initial_config` mode**: Skips password verification, sets new password from input
- **Backup password**: `settings_backup_fn` requires `auth_password` in JSON body before returning backup data
- **Restore password**: JS injects `auth_password` from form input into the JSON body

## Flash Layout

- **App slot**: Main firmware binary
- **Metadata sector**: Firmware metadata (size, CRC, etc.)
- **Staging area**: Where firmware upload is staged before install
- **Backup sector**: At `0x1FE000`, contains settings backup that survives firmware install
- **Install process**: `apply_install_impl` erases app slot + metadata, writes new firmware, triggers reboot

## JSON Handling

- **Encoded format** (`"encoded":true`): Passwords are hex-encoded, decoded via `hex_decode()` then `obfuscate()`
- **Non-encoded format** (`"encoded":false`): Passwords are plaintext, copied directly via `strncpy()`
- **`get_settings_backup_json()`**: Produces encoded format (reads stored passwords, obfuscates, hex-encodes)
- **`apply_settings_from_json()`**: Consumes both formats (checks `encoded` flag)
- **`settings_backup_fn`**: HTTP handler for `/settings_backup.cgi`, requires `auth_password` in JSON body
- **`settings_save_fn`**: HTTP handler for `/settings_save.cgi`, checks `auth_password` before applying settings
