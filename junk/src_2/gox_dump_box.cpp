#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <iostream>

static bool read_all(const char* path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path, "rb"); if (!f) return false;
    std::vector<uint8_t> buf(1<<16);
    while (true) {
        size_t r = std::fread(buf.data(), 1, buf.size(), f);
        if (r) out.insert(out.end(), buf.begin(), buf.begin()+r);
        if (r < buf.size()) break;
    }
    std::fclose(f); return true;
}

static uint32_t get_le32(const uint8_t* p){ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }

int main(int argc, char** argv){
    if (argc < 2) { std::fprintf(stderr, "Usage: %s file.gox\n", argv[0]); return 1; }
    std::vector<uint8_t> d; if (!read_all(argv[1], d)) { std::perror("read"); return 2; }
    if (d.size() < 12 || std::memcmp(d.data(), "GOX ", 4) != 0) { std::fprintf(stderr, "Not a GOX file\n"); return 3; }
    // List chunks and decode IMG->box
    size_t p = 8;
    while (p + 8 <= d.size()) {
        char id[5] = {0}; std::memcpy(id, &d[p], 4); p += 4; uint32_t sz = get_le32(&d[p]); p += 4;
        std::printf("chunk '%s' len=%u at 0x%zx\n", id, sz, p-8);
        if (std::strncmp(id, "IMG ", 4) == 0) {
            size_t q = p; if (q + 4 <= d.size()) {
                uint32_t klen = get_le32(&d[q]); q += 4;
                if (q + klen + 4 <= d.size()) {
                    std::string key((const char*)&d[q], klen); q += klen; uint32_t vlen = get_le32(&d[q]); q += 4;
                    std::printf("  dict key='%s' vlen=%u\n", key.c_str(), vlen);
                    if (key == "box" && q + vlen <= d.size()) {
                        const float* fp = reinterpret_cast<const float*>(&d[q]); size_t nf=vlen/4; std::printf("  box floats (%zu)\n", nf);
                        for (size_t j=0;j<nf;j++) std::printf("    f[%2zu]=%g\n", j, (double)fp[j]);
                    }
                }
            }
        }
        // skip content and CRC
        p += sz;
        if (p + 4 <= d.size()) p += 4; else break;
    }
    return 0;
}
