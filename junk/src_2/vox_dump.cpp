// Minimal MagicaVoxel .vox dumper
// Reads a .vox file from stdin and prints a readable summary.
// No external dependencies; parses both VOX 150+ and the old format.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>
#include <iomanip>

struct Cursor {
    const uint8_t* p;
    size_t n;
};

static bool take(Cursor& c, void* out, size_t sz) {
    if (c.n < sz) return false;
    if (out && sz) std::memcpy(out, c.p, sz);
    c.p += sz; c.n -= sz; return true;
}

static bool take_u8(Cursor& c, uint8_t& v) { return take(c, &v, 1); }

static bool take_u32(Cursor& c, uint32_t& v) {
    if (c.n < 4) return false;
    v = (uint32_t)c.p[0] | ((uint32_t)c.p[1] << 8) | ((uint32_t)c.p[2] << 16) | ((uint32_t)c.p[3] << 24);
    c.p += 4; c.n -= 4; return true;
}

static bool take_i32(Cursor& c, int32_t& v) { uint32_t t; if (!take_u32(c, t)) return false; v = (int32_t)t; return true; }

static void print_indent(int k) { for (int i=0;i<k;i++) std::cout << ' '; }

static std::string take_string(Cursor& c) {
    int32_t sz = 0; if (!take_i32(c, sz) || sz < 0 || (size_t)sz > c.n) return std::string();
    std::string s((const char*)c.p, (size_t)sz);
    c.p += sz; c.n -= sz; return s;
}

struct VoxModelInfo { uint32_t w=0,h=0,d=0; uint32_t voxels=0; };

static void dump_palette_preview(const uint8_t (*pal)[4], int count, int indent) {
    int show = count < 16 ? count : 16;
    for (int i=0;i<show;i++) {
        print_indent(indent);
        std::cout << i << ": rgba("
                  << (int)pal[i][0] << "," << (int)pal[i][1] << ","
                  << (int)pal[i][2] << "," << (int)pal[i][3] << ")\n";
    }
    if (count > show) { print_indent(indent); std::cout << "... (" << (count-show) << " more)\n"; }
}

static bool parse_vox_new(Cursor& c) {
    // After header, expect MAIN chunk with children.
    auto parse_chunk = [&](auto& self, Cursor& cur, int depth) -> bool {
        if (cur.n < 12) return false;
        char id[5] = {0};
        std::memcpy(id, cur.p, 4); cur.p += 4; cur.n -= 4;
        uint32_t content = 0, children = 0;
        if (!take_u32(cur, content) || !take_u32(cur, children)) return false;
        if (cur.n < content + children) return false;
        Cursor content_cur{cur.p, content};
        Cursor children_cur{cur.p + content, children};
        cur.p += content + children; cur.n -= (content + children);

        print_indent(depth); std::cout << "Chunk " << id << " (content=" << content << ", children=" << children << ")\n";

        if (std::strncmp(id, "SIZE", 4) == 0) {
            uint32_t w=0,h=0,d=0;
            if (!take_u32(content_cur, w) || !take_u32(content_cur, h) || !take_u32(content_cur, d)) {
                print_indent(depth+2); std::cout << "<truncated SIZE>\n";
            } else {
                print_indent(depth+2); std::cout << "w="<<w<<" h="<<h<<" d="<<d<<"\n";
            }
        } else if (std::strncmp(id, "XYZI", 4) == 0) {
            uint32_t nvox=0; if (!take_u32(content_cur, nvox)) { print_indent(depth+2); std::cout << "<truncated XYZI>\n"; }
            else {
                print_indent(depth+2); std::cout << "voxels="<<nvox<<"\n";
                size_t need = (size_t)nvox * 4;
                if (content_cur.n < need) { print_indent(depth+2); std::cout << "<truncated voxel list>\n"; }
                else { content_cur.p += need; content_cur.n -= need; }
            }
        } else if (std::strncmp(id, "RGBA", 4) == 0) {
            // 256 RGBA entries; official spec uses indices 1..255, but chunk has 256 entries.
            if (content_cur.n >= 1024) {
                const uint8_t* p = content_cur.p;
                print_indent(depth+2); std::cout << "palette: 256 colors (showing first 16 from index 0)\n";
                const uint8_t (*pal)[4] = reinterpret_cast<const uint8_t (*)[4]>(p);
                dump_palette_preview(pal, 256, depth+4);
            } else {
                print_indent(depth+2); std::cout << "palette: <truncated>\n";
            }
        } else if (std::strncmp(id, "nTRN", 4) == 0) {
            int32_t node_id=0; take_i32(content_cur, node_id);
            print_indent(depth+2); std::cout << "node_id="<<node_id<<"\n";
            // node attributes
            int32_t nb=0; if (!take_i32(content_cur, nb)) nb = 0; else {
                print_indent(depth+2); std::cout << "dict_entries="<<nb<<"\n";
                for (int i=0;i<nb;i++) {
                    std::string k = take_string(content_cur);
                    std::string v = take_string(content_cur);
                    print_indent(depth+4); std::cout << k << ": " << v << "\n";
                }
            }
            int32_t child_id=0; take_i32(content_cur, child_id);
            print_indent(depth+2); std::cout << "child_id="<<child_id<<"\n";
            // two reserved ints
            int32_t r1=0,r2=0; take_i32(content_cur, r1); take_i32(content_cur, r2);
            int32_t frames=0; take_i32(content_cur, frames);
            print_indent(depth+2); std::cout << "frames="<<frames<<"\n";
            for (int i=0;i<frames;i++) {
                int32_t fnb=0; if (!take_i32(content_cur, fnb)) break;
                print_indent(depth+4); std::cout << "frame "<<i<<" entries="<<fnb<<"\n";
                for (int j=0;j<fnb;j++) {
                    std::string k = take_string(content_cur);
                    std::string v = take_string(content_cur);
                    print_indent(depth+6); std::cout << k << ": " << v << "\n";
                }
            }
        } else if (std::strncmp(id, "nSHP", 4) == 0) {
            int32_t node_id=0; take_i32(content_cur, node_id);
            print_indent(depth+2); std::cout << "node_id="<<node_id<<"\n";
            int32_t nb=0; if (!take_i32(content_cur, nb)) nb=0; else {
                // dict (ignored but printed)
                print_indent(depth+2); std::cout << "dict_entries="<<nb<<"\n";
                for (int i=0;i<nb;i++) { std::string k = take_string(content_cur); std::string v = take_string(content_cur); print_indent(depth+4); std::cout << k << ": " << v << "\n"; }
            }
            int32_t nm=0; take_i32(content_cur, nm);
            print_indent(depth+2); std::cout << "models="<<nm<<"\n";
            for (int i=0;i<nm;i++) {
                int32_t model_id=0; take_i32(content_cur, model_id);
                print_indent(depth+4); std::cout << "model_id="<<model_id;
                int32_t mdict=0; if (take_i32(content_cur, mdict)) {
                    std::cout << " dict_entries="<<mdict<<"\n";
                    for (int j=0;j<mdict;j++) { std::string k = take_string(content_cur); std::string v = take_string(content_cur); print_indent(depth+6); std::cout << k << ": " << v << "\n"; }
                } else {
                    std::cout << "\n";
                }
            }
        } else if (std::strncmp(id, "nGRP", 4) == 0) {
            int32_t node_id=0; take_i32(content_cur, node_id);
            print_indent(depth+2); std::cout << "node_id="<<node_id<<"\n";
            int32_t nb=0; if (!take_i32(content_cur, nb)) nb=0; else {
                print_indent(depth+2); std::cout << "dict_entries="<<nb<<"\n";
                for (int i=0;i<nb;i++) { std::string k = take_string(content_cur); std::string v = take_string(content_cur); print_indent(depth+4); std::cout << k << ": " << v << "\n"; }
            }
            int32_t ch=0; take_i32(content_cur, ch);
            print_indent(depth+2); std::cout << "children="<<ch<<"\n";
            for (int i=0;i<ch;i++) { int32_t cid=0; take_i32(content_cur, cid); print_indent(depth+4); std::cout << "child_id="<<cid<<"\n"; }
        }

        // Recurse into children bytes
        while (children_cur.n > 0) {
            if (!self(self, children_cur, depth + 2)) break;
        }
        return true;
    };

    while (c.n > 0) {
        if (!parse_chunk(parse_chunk, c, 0)) break;
    }
    return true;
}

static bool parse_vox_old(Cursor& c) {
    // Old style: d,h,w then w*h*d bytes of indices, then 256*3 palette bytes
    auto get_u32 = [&](uint32_t& v){ if (c.n < 4) return false; v = (uint32_t)c.p[0] | ((uint32_t)c.p[1] << 8) | ((uint32_t)c.p[2] << 16) | ((uint32_t)c.p[3] << 24); c.p+=4; c.n-=4; return true; };
    uint32_t d=0,h=0,w=0;
    if (!get_u32(d) || !get_u32(h) || !get_u32(w)) { std::cerr << "Error: truncated old .vox dims" << std::endl; return false; }
    std::cout << "Old MagicaVoxel format\n";
    std::cout << "dims: w="<<w<<" h="<<h<<" d="<<d<<"\n";
    uint64_t total = (uint64_t)w*h*d;
    if (c.n < total) { std::cerr << "Error: truncated voxel data" << std::endl; return false; }
    // Compute histogram of used palette indices
    std::vector<uint32_t> hist(256,0);
    for (uint64_t i=0;i<total;i++) { hist[c.p[i]]++; }
    c.p += total; c.n -= total;
    if (c.n < 256*3) { std::cerr << "Warning: truncated palette" << std::endl; }
    else { c.p += 256*3; c.n -= 256*3; }
    std::cout << "voxel_count: "<< total << " (non-empty: " << (total - hist[255]) << ")\n";
    std::cout << "used_palette_indices:";
    int shown=0;
    for (int i=0;i<256;i++) if (hist[i]) { if (shown++ % 16 == 0) std::cout << "\n  "; std::cout << i << "("<<hist[i]<<") "; }
    if (!shown) std::cout << " none";
    std::cout << "\n";
    return true;
}

int main() {
    // Slurp stdin into memory for simpler parsing.
    std::vector<uint8_t> buf;
    {
        std::vector<uint8_t> chunk(1<<16);
        while (true) {
            size_t r = std::fread(chunk.data(), 1, chunk.size(), stdin);
            if (r == 0) break;
            buf.insert(buf.end(), chunk.begin(), chunk.begin() + r);
        }
    }
    if (buf.size() < 4) { std::fprintf(stderr, "Error: input too small.\n"); return 1; }

    Cursor cur{buf.data(), buf.size()};

    // Check magic
    if (cur.n >= 4 && std::memcmp(cur.p, "VOX ", 4) == 0) {
        cur.p += 4; cur.n -= 4;
        uint32_t version = 0; if (!take_u32(cur, version)) { std::fprintf(stderr, "Error: truncated version.\n"); return 2; }
        std::cout << "VOX file\n";
        std::cout << "version: " << version << "\n";
        if (!parse_vox_new(cur)) { std::fprintf(stderr, "Error: failed parsing vox.\n"); return 3; }
        return 0;
    } else {
        // Try old format
        if (!parse_vox_old(cur)) return 4;
        return 0;
    }
}

