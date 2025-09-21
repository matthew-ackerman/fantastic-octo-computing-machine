#include "hash_utils.h"

#include <fstream>
#include <cstdint>
#include <cstdio>

std::string hash_file_fnv1a64(const char* path) {
    std::ifstream f(path ? path : "", std::ios::binary);
    if (!f.good()) return std::string();
    const uint64_t FNV_OFFSET = 1469598103934665603ull;
    const uint64_t FNV_PRIME  = 1099511628211ull;
    uint64_t h = FNV_OFFSET;
    char buf[4096];
    while (f.good()) {
        f.read(buf, sizeof(buf));
        std::streamsize n = f.gcount();
        for (std::streamsize i = 0; i < n; ++i) { h ^= (uint8_t)buf[i]; h *= FNV_PRIME; }
    }
    char hex[17]; std::snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)h);
    return std::string(hex);
}

