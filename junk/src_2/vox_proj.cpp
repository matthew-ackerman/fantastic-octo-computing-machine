#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <functional>
#include <array>

// Minimal VOX (MagicaVoxel) -> orthographic PNG projector with transparent background.
// - Parses basic VOX: SIZE + XYZI (+ optional RGBA, ignored). Handles multiple chunks; merges all XYZI into one volume.
// - Projects voxels orthographically from spherical angles (theta,phi,psi) described in a JSON array.
// - Outputs one PNG per JSON entry (object field 'out'). Background alpha=0; foreground solid color.

struct View { std::string out; double r=1.0, theta=0.0, phi=0.0, psi=0.0; };

static bool read_all(const char* path, std::vector<uint8_t>& out){
    FILE* f = std::fopen(path, "rb"); if (!f) return false;
    std::vector<uint8_t> buf(1<<16);
    while (true){ size_t r = std::fread(buf.data(),1,buf.size(),f); if (r) out.insert(out.end(), buf.begin(), buf.begin()+r); if (r<buf.size()) break; }
    std::fclose(f); return true;
}

// Tiny PNG writer (RGBA, no external deps)
static uint32_t crc32_update(uint32_t crc, const uint8_t* buf, size_t len){
    static uint32_t table[256]; static bool init=false; if (!init){
        for (uint32_t i=0;i<256;i++){ uint32_t c=i; for (int k=0;k<8;k++) c = (c & 1) ? (0xEDB88320u ^ (c>>1)) : (c>>1); table[i]=c; }
        init=true;
    }
    crc ^= 0xFFFFFFFFu;
    for (size_t i=0;i<len;i++) crc = table[(crc ^ buf[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}
static uint32_t adler32(const uint8_t* data, size_t len){ const uint32_t MOD=65521u; uint32_t a=1,b=0; for (size_t i=0;i<len;i++){ a+=data[i]; if (a>=MOD) a-=MOD; b+=a; if (b>=MOD) b%=MOD;} return (b<<16)|a; }
static bool write_png_rgba(const char* path, int w, int h, const std::vector<uint8_t>& rgba){
    FILE* f = std::fopen(path, "wb"); if (!f) return false;
    auto be32=[&](uint32_t v){ uint8_t b[4]={(uint8_t)(v>>24),(uint8_t)(v>>16),(uint8_t)(v>>8),(uint8_t)v}; std::fwrite(b,1,4,f); };
    auto chunk=[&](const char* type, const std::vector<uint8_t>& data){ uint32_t len=data.size(); be32(len); std::fwrite(type,1,4,f); uint32_t crc=0; crc=crc32_update(crc,(const uint8_t*)type,4); if (len) crc=crc32_update(crc,data.data(),len); std::fwrite(data.data(),1,len,f); be32(crc); };
    static const uint8_t sig[8]={137,80,78,71,13,10,26,10}; std::fwrite(sig,1,8,f);
    std::vector<uint8_t> ihdr(13,0);
    ihdr[0]=(uint8_t)(w>>24); ihdr[1]=(uint8_t)(w>>16); ihdr[2]=(uint8_t)(w>>8); ihdr[3]=(uint8_t)w;
    ihdr[4]=(uint8_t)(h>>24); ihdr[5]=(uint8_t)(h>>16); ihdr[6]=(uint8_t)(h>>8); ihdr[7]=(uint8_t)h;
    ihdr[8]=8; ihdr[9]=6; ihdr[10]=0; ihdr[11]=0; ihdr[12]=0; chunk("IHDR", ihdr);
    std::vector<uint8_t> raw; raw.reserve((size_t)(w*4+1)*h);
    for (int y=0;y<h;y++){ raw.push_back(0); const uint8_t* row=&rgba[(size_t)y*w*4]; raw.insert(raw.end(), row, row+(size_t)w*4); }
    std::vector<uint8_t> z; z.push_back(0x78); z.push_back(0x01); size_t pos=0, remain=raw.size();
    while (remain>0){ uint16_t block=(uint16_t)std::min<size_t>(remain,65535u); uint8_t bfinal=(remain<=65535u)?1:0; z.push_back(bfinal); z.push_back(block&0xFF); z.push_back((block>>8)&0xFF); uint16_t nlen=~block; z.push_back(nlen&0xFF); z.push_back((nlen>>8)&0xFF); z.insert(z.end(), raw.begin()+pos, raw.begin()+pos+block); pos+=block; remain-=block; }
    uint32_t ad=adler32(raw.data(), raw.size()); z.push_back((uint8_t)(ad>>24)); z.push_back((uint8_t)(ad>>16)); z.push_back((uint8_t)(ad>>8)); z.push_back((uint8_t)ad);
    chunk("IDAT", z); chunk("IEND", {}); std::fclose(f); return true;
}

// Minimal JSON angles parser (array of {out,r,theta,phi,psi})
static bool parse_angles_json(const char* path, std::vector<View>& out){
    std::vector<uint8_t> buf; if (!read_all(path, buf)) return false; std::string s((const char*)buf.data(), buf.size()); size_t pos=0; bool any=false;
    while (true){ size_t l=s.find('{',pos); if (l==std::string::npos) break; size_t r=s.find('}',l+1); if (r==std::string::npos) break; std::string obj=s.substr(l+1,r-l-1);
        View v; auto fnum=[&](const char* key,double def){ size_t k=obj.find(key); if (k==std::string::npos) return def; k=obj.find(':',k); if (k==std::string::npos) return def; k++; while(k<obj.size() && isspace((unsigned char)obj[k])) k++; char* e=nullptr; double val=strtod(obj.c_str()+k,&e); return val; };
        auto fstr=[&](const char* key){ size_t k=obj.find(key); if (k==std::string::npos) return std::string(); k=obj.find(':',k); if (k==std::string::npos) return std::string(); k=obj.find('"',k); if (k==std::string::npos) return std::string(); size_t e=obj.find('"',k+1); if (e==std::string::npos) return std::string(); return obj.substr(k+1,e-(k+1)); };
        v.out=fstr("out"); v.r=fnum("\"r\"",1.0); v.theta=fnum("\"theta\"",0.0); v.phi=fnum("\"phi\"",0.0); v.psi=fnum("\"psi\"",0.0);
        out.push_back(v); any=true; pos=r+1; }
    return any;
}

struct VoxModel {
    int w=0,h=0,d=0;
    std::vector<uint8_t> idx; // color index per voxel (0=empty)
    uint8_t pal[256][4]{};    // palette RGBA
    bool has_pal=false;
};

static bool parse_vox(const char* path, VoxModel& model){
    std::vector<uint8_t> buf; if (!read_all(path, buf)) { std::fprintf(stderr, "vox: failed read %s\n", path); return false; }
    if (buf.size()<8 || std::memcmp(buf.data(), "VOX ", 4)!=0) { std::fprintf(stderr, "vox: bad header\n"); return false; }
    auto rd32=[&](const uint8_t* p){ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); };
    size_t p=8; // after magic+version
    if (p + 12 > buf.size()) return false;
    if (std::memcmp(&buf[p], "MAIN", 4)!=0) { std::fprintf(stderr, "vox: MAIN not found\n"); return false; }
    p += 4; uint32_t content = rd32(&buf[p]); p+=4; uint32_t children = rd32(&buf[p]); p+=4; (void)content;
    size_t q = p; size_t qend = p + children; if (qend > buf.size()) return false;
    while (q + 12 <= qend){
        const char* cid = (const char*)&buf[q]; q+=4; uint32_t csz = rd32(&buf[q]); q+=4; uint32_t cchild = rd32(&buf[q]); q+=4;
        size_t cstart = q; size_t cend = q + csz; size_t cnext = cend + cchild;
        if (cend > buf.size() || cnext > buf.size()) return false;
        if (std::strncmp(cid, "SIZE", 4) == 0){
            if (csz>=12){ model.w=(int)rd32(&buf[cstart+0]); model.h=(int)rd32(&buf[cstart+4]); model.d=(int)rd32(&buf[cstart+8]); }
        } else if (std::strncmp(cid, "XYZI", 4) == 0){
            if (csz>=4){ uint32_t n=rd32(&buf[cstart]); size_t need=4 + (size_t)n*4; if (csz>=need){ if (model.w && model.h && model.d){ if (model.idx.empty()) model.idx.assign((size_t)model.w*model.h*model.d, 0); for (uint32_t i=0;i<n;i++){ uint8_t x=buf[cstart+4+i*4+0], y=buf[cstart+4+i*4+1], z=buf[cstart+4+i*4+2], ci=buf[cstart+4+i*4+3]; if (x<model.w && y<model.h && z<model.d){ size_t ii=(size_t)z*model.w*model.h + (size_t)y*model.w + x; model.idx[ii]=ci; } } } } }
        } else if (std::strncmp(cid, "RGBA", 4) == 0){
            if (csz>=256*4){ const uint8_t* ppal = &buf[cstart]; for (int i=0;i<256;i++){ model.pal[i][0]=ppal[i*4+0]; model.pal[i][1]=ppal[i*4+1]; model.pal[i][2]=ppal[i*4+2]; model.pal[i][3]=ppal[i*4+3]; } model.has_pal=true; }
        }
        q = cnext;
    }
    if (!model.has_pal) {
        // Fallback: simple HSV-like gradient
        for (int i=0;i<256;i++){ uint8_t r=(uint8_t)((std::sin(i*0.024f+0.0f)*0.5f+0.5f)*255); uint8_t g=(uint8_t)((std::sin(i*0.024f+2.09f)*0.5f+0.5f)*255); uint8_t b=(uint8_t)((std::sin(i*0.024f+4.18f)*0.5f+0.5f)*255); model.pal[i][0]=r; model.pal[i][1]=g; model.pal[i][2]=b; model.pal[i][3]=255; }
    }
    if (model.w<=0 || model.h<=0 || model.d<=0 || model.idx.empty()) { std::fprintf(stderr, "vox: missing SIZE/XYZI\n"); return false; }
    return true;
}

int main(int argc, char** argv){
    const char* angles_path=nullptr; const char* vox_path=nullptr; int W=512, H=512; int thick=1; uint8_t fg[4]={255,255,255,255};
    for (int i=1;i<argc;i++){
        std::string a=argv[i];
        if (a=="--angles" && i+1<argc) angles_path=argv[++i];
        else if (a=="--size" && i+1<argc){ std::string s=argv[++i]; size_t x=s.find('x'); if (x==std::string::npos) x=s.find('X'); if (x!=std::string::npos){ W=std::atoi(s.substr(0,x).c_str()); H=std::atoi(s.substr(x+1).c_str()); } }
        else if (a=="--thick" && i+1<argc){ thick=std::max(0, std::atoi(argv[++i])); }
        else if (a=="--color" && i+4<argc){ fg[0]=std::atoi(argv[++i]); fg[1]=std::atoi(argv[++i]); fg[2]=std::atoi(argv[++i]); fg[3]=std::atoi(argv[++i]); }
        else if (a[0] != '-' && !vox_path){ vox_path = argv[i]; }
        else { std::fprintf(stderr, "Usage: %s --angles angles.json [--size WxH] [--thick N] [--color R G B A] input.vox\n", argv[0]); return 1; }
    }
    if (!angles_path || !vox_path){ std::fprintf(stderr, "Missing --angles or input.vox\n"); return 2; }
    std::vector<View> views; if (!parse_angles_json(angles_path, views)){ std::fprintf(stderr, "Failed to parse angles\n"); return 3; }

    VoxModel model; if (!parse_vox(vox_path, model)) return 4;

    // Precompute voxel positions list and color index for speed
    struct V { int x,y,z; uint8_t ci; };
    std::vector<V> voxels; voxels.reserve(model.idx.size()/4);
    for (int z=0; z<model.d; ++z){
        for (int y=0; y<model.h; ++y){
            for (int x=0; x<model.w; ++x){
                size_t ii=(size_t)z*model.w*model.h + (size_t)y*model.w + x;
                uint8_t ci = model.idx[ii]; if (ci) voxels.push_back(V{x,y,z,ci});
            }
        }
    }
    if (voxels.empty()) { std::fprintf(stderr, "vox: no filled voxels\n"); return 5; }
    // Center offset
    double cx0 = (model.w - 1) * 0.5; double cy0 = (model.h - 1) * 0.5; double cz0 = (model.d - 1) * 0.5;

    auto normalize3=[&](float v[3]){ float n=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); if (n>0){ v[0]/=n; v[1]/=n; v[2]/=n; } };
    auto cross3=[&](const float a[3], const float b[3], float o[3]){ o[0]=a[1]*b[2]-a[2]*b[1]; o[1]=a[2]*b[0]-a[0]*b[2]; o[2]=a[0]*b[1]-a[1]*b[0]; };

    for (const auto& v : views){
        // Build camera basis from spherical (same convention as earlier):
        double st=std::sin(v.theta), ct=std::cos(v.theta); double sp=std::sin(v.phi), cp=std::cos(v.phi);
        float fwd[3] = { (float)(sp*ct), (float)(sp*st), (float)(cp) };
        float zc[3] = { -fwd[0], -fwd[1], -fwd[2] }; normalize3(zc);
        float upw[3] = {0,1,0}; float xc[3]; cross3(upw, zc, xc); normalize3(xc); float yc[3]; cross3(zc, xc, yc); normalize3(yc);
        if (v.psi != 0.0){ double c=std::cos(v.psi), s=std::sin(v.psi); float x2[3]={ (float)(c*xc[0] + s*yc[0]), (float)(c*xc[1] + s*yc[1]), (float)(c*xc[2] + s*yc[2]) }; float y2[3]={ (float)(-s*xc[0] + c*yc[0]), (float)(-s*xc[1] + c*yc[1]), (float)(-s*xc[2] + c*yc[2]) }; xc[0]=x2[0]; xc[1]=x2[1]; xc[2]=x2[2]; yc[0]=y2[0]; yc[1]=y2[1]; yc[2]=y2[2]; }

        // First pass: compute projected bounds
        float xmin=1e9f,xmax=-1e9f,ymin=1e9f,ymax=-1e9f;
        for (const auto& P : voxels){
            float x=(float)(P.x - cx0), y=(float)(P.y - cy0), z=(float)(P.z - cz0);
            float u = x*xc[0] + y*xc[1] + z*xc[2];
            float wv = x*yc[0] + y*yc[1] + z*yc[2];
            if (u<xmin) xmin=u; if (u>xmax) xmax=u; if (wv<ymin) ymin=wv; if (wv>ymax) ymax=wv;
        }
        float sx = (float)W / std::max(1e-6f, (xmax-xmin));
        float sy = (float)H / std::max(1e-6f, (ymax-ymin));
        float s = 0.95f * std::min(sx, sy) * (float)v.r; // small margin + per-view scale
        float ux = 0.5f * (xmin+xmax);
        float uy = 0.5f * (ymin+ymax);

        std::vector<uint8_t> img((size_t)W*H*4, 0);
        std::vector<float> zbuf((size_t)W*H, -1e30f);
        auto plot=[&](int px,int py, float depth, uint8_t ci){
            if ((unsigned)px>=(unsigned)W||(unsigned)py>=(unsigned)H) return;
            size_t i2=(size_t)py*W + px; if (depth <= zbuf[i2]) return; zbuf[i2]=depth;
            size_t i=((size_t)py*W+px)*4; const uint8_t* c = model.pal[ci ? ci : 0];
            img[i+0]=c[0]; img[i+1]=c[1]; img[i+2]=c[2]; img[i+3]=255; // opaque voxel, transparent bg elsewhere
        };

        // Second pass: draw
        // Compute voxel footprint in pixels and draw filled squares to avoid gaps
        int footprint = (int)std::ceil(s);
        if (footprint < 1) footprint = 1;
        int half_fp = footprint/2;
        for (const auto& P : voxels){
            float x=(float)(P.x - cx0), y=(float)(P.y - cy0), z=(float)(P.z - cz0);
            float du = (x*xc[0] + y*xc[1] + z*xc[2]);
            float dv = (x*yc[0] + y*yc[1] + z*yc[2]);
            float depth = (x*zc[0] + y*zc[1] + z*zc[2]); // larger = closer to camera
            int u = (int)std::round(( du - ux) * s + (W*0.5f));
            int v = (int)std::round(( dv - uy) * s + (H*0.5f));
            if (thick>1) {
                int r = thick; for (int dy=-r; dy<=r; ++dy) for (int dx=-r; dx<=r; ++dx) plot(u+dx, v+dy, depth, P.ci);
            } else {
                for (int yy=-half_fp; yy<=half_fp; ++yy)
                    for (int xx=-half_fp; xx<=half_fp; ++xx)
                        plot(u+xx, v+yy, depth, P.ci);
            }
        }

        const char* outpath = v.out.empty() ? "out.png" : v.out.c_str();
        if (!write_png_rgba(outpath, W, H, img)) { std::fprintf(stderr, "write failed: %s\n", outpath); return 6; }
    }
    return 0;
}
