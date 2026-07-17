#pragma once

#include <cstring>

static inline void safe_strncpy(char *dst, const char *src, size_t n) {
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
}
