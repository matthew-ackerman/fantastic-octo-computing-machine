#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

struct View { std::string out; double r=1.0, theta=0.0, phi=0.0, psi=0.0; };

static bool read_all(const char* path, std::vector<uint8_t>& out){
    FILE* f = std::fopen(path, "rb"); if (!f) return false;
    std::vector<uint8_t> buf(1<<16);
    while (true){ size_t r = std::fread(buf.data(),1,buf.size(),f); if (r) out.insert(out.end(), buf.begin(), buf.begin()+r); if (r<buf.size()) break; }
    std::fclose(f); return true;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t* buf, size_t len){
    static uint32_t table[256]; static bool init=false; if (!init){
        for (uint32_t i=0;i<256;i++){ uint32_t c=i; for (int k=0;k<8;k++) c = (c & 1) ? (0xEDB88320u ^ (c>>1)) : (c>>1); table[i]=c; }
        init=true;
    }
    crc ^= 0xFFFFFFFFu;
    for (size_t i=0;i<len;i++) crc = table[(crc ^ buf[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

static uint32_t adler32(const uint8_t* data, size_t len){
    const uint32_t MOD = 65521u; uint32_t a=1, b=0;
    for (size_t i=0;i<len;i++){ a += data[i]; if (a>=MOD) a-=MOD; b += a; if (b>=MOD) b%=MOD; }
    return (b<<16) | a;
}

static bool write_png_rgba(const char* path, int w, int h, const std::vector<uint8_t>& rgba){
    FILE* f = std::fopen(path, "wb"); if (!f) return false;
    auto be32=[&](uint32_t v){ uint8_t b[4] = { (uint8_t)(v>>24),(uint8_t)(v>>16),(uint8_t)(v>>8),(uint8_t)(v) }; std::fwrite(b,1,4,f); };
    auto chunk=[&](const char* type, const std::vector<uint8_t>& data){ uint32_t len=data.size(); be32(len); std::fwrite(type,1,4,f); uint32_t crc=0; crc = crc32_update(crc,(const uint8_t*)type,4); if (len) crc = crc32_update(crc,data.data(),len); std::fwrite(data.data(),1,len,f); be32(crc); };
    // signature
    static const uint8_t sig[8] = {137,80,78,71,13,10,26,10}; std::fwrite(sig,1,8,f);
    // IHDR
    std::vector<uint8_t> ihdr(13,0);
    ihdr[0]=(uint8_t)(w>>24); ihdr[1]=(uint8_t)(w>>16); ihdr[2]=(uint8_t)(w>>8); ihdr[3]=(uint8_t)w;
    ihdr[4]=(uint8_t)(h>>24); ihdr[5]=(uint8_t)(h>>16); ihdr[6]=(uint8_t)(h>>8); ihdr[7]=(uint8_t)h;
    ihdr[8]=8; // bit depth
    ihdr[9]=6; // RGBA
    ihdr[10]=0; ihdr[11]=0; ihdr[12]=0; // comp/filter/interlace
    chunk("IHDR", ihdr);
    // raw scanlines with filter 0
    std::vector<uint8_t> raw; raw.reserve((size_t)(w*4+1)*h);
    for (int y=0;y<h;y++){
        raw.push_back(0);
        const uint8_t* row=&rgba[(size_t)y*w*4]; raw.insert(raw.end(), row, row + (size_t)w*4);
    }
    // zlib: no compression, type 0 stored blocks
    std::vector<uint8_t> z;
    z.push_back(0x78); z.push_back(0x01); // zlib header (no compression, 32K window)
    size_t pos=0; size_t remain=raw.size();
    while (remain>0){
        uint16_t block = (uint16_t)std::min<size_t>(remain, 65535u);
        uint8_t bfinal = (remain<=65535u) ? 1 : 0;
        z.push_back(bfinal); // BTYPE=00
        z.push_back(block & 0xFF); z.push_back((block>>8)&0xFF);
        uint16_t nlen = ~block; z.push_back(nlen & 0xFF); z.push_back((nlen>>8)&0xFF);
        z.insert(z.end(), raw.begin()+pos, raw.begin()+pos+block);
        pos += block; remain -= block;
    }
    uint32_t ad = adler32(raw.data(), raw.size());
    z.push_back((uint8_t)(ad>>24)); z.push_back((uint8_t)(ad>>16)); z.push_back((uint8_t)(ad>>8)); z.push_back((uint8_t)ad);
    chunk("IDAT", z);
    // IEND
    chunk("IEND", {});
    std::fclose(f); return true;
}

static bool parse_angles_json(const char* path, std::vector<View>& out){
    std::vector<uint8_t> buf; if (!read_all(path, buf)) return false;
    std::string s((const char*)buf.data(), buf.size()); size_t pos=0; bool any=false;
    while (true){ size_t l=s.find('{',pos); if (l==std::string::npos) break; size_t r=s.find('}',l+1); if (r==std::string::npos) break; std::string obj=s.substr(l+1,r-l-1);
        View v; auto fnum=[&](const char* key,double def){ size_t k=obj.find(key); if (k==std::string::npos) return def; k=obj.find(':',k); if (k==std::string::npos) return def; k++; while(k<obj.size() && isspace((unsigned char)obj[k])) k++; char* e=nullptr; double val=strtod(obj.c_str()+k,&e); return val; };
        auto fstr=[&](const char* key){ size_t k=obj.find(key); if (k==std::string::npos) return std::string(); k=obj.find(':',k); if (k==std::string::npos) return std::string(); k=obj.find('"',k); if (k==std::string::npos) return std::string(); size_t e=obj.find('"',k+1); if (e==std::string::npos) return std::string(); return obj.substr(k+1,e-(k+1)); };
        v.out=fstr("out"); v.r=fnum("\"r\"",1.0); v.theta=fnum("\"theta\"",0.0); v.phi=fnum("\"phi\"",0.0); v.psi=fnum("\"psi\"",0.0);
        out.push_back(v); any=true; pos=r+1; }
    return any;
}

static bool parse_gox_box_extents(const char* path, float half_extents[3]){
    std::vector<uint8_t> d; if (!read_all(path,d)) return false; if (d.size()<12 || std::memcmp(d.data(),"GOX ",4)!=0) return false;
    auto rd=[&](const uint8_t* p){ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); };
    for (size_t i=0;i+11<d.size();i++){
        if (d[i]==3 && d[i+1]==0 && d[i+2]==0 && d[i+3]==0 && d[i+4]=='b' && d[i+5]=='o' && d[i+6]=='x'){
            size_t p=i+7; if (p+4>d.size()) break; uint32_t sz=rd(&d[p]); p+=4; if (p+sz>d.size()) break;
            if (sz==24){ const float* fp=(const float*)&d[p]; float minv[3]={fp[0],fp[1],fp[2]}; float maxv[3]={fp[3],fp[4],fp[5]}; for (int k=0;k<3;k++) half_extents[k]=0.5f*std::fabs(maxv[k]-minv[k]); return true; }
            if (sz==64){ const float* fp=(const float*)&d[p]; half_extents[0]=std::fabs(fp[0]); half_extents[1]=std::fabs(fp[5]); half_extents[2]=std::fabs(fp[10]); return true; }
            break;
        }
    }
    return false;
}

int main(int argc, char** argv){
    const char* angles_path=nullptr; int W=512, H=512; int grid=64; float half_ext[3]={1.0f,1.0f,1.0f}; uint8_t fg[4]={255,255,255,255}; const char* gox_path=nullptr;
    for (int i=1;i<argc;i++){
        std::string a=argv[i];
        if (a=="--angles" && i+1<argc) angles_path=argv[++i];
        else if (a=="--size" && i+1<argc){ std::string s=argv[++i]; size_t x=s.find('x'); if (x==std::string::npos) x=s.find('X'); if (x!=std::string::npos){ W=std::atoi(s.substr(0,x).c_str()); H=std::atoi(s.substr(x+1).c_str()); } }
        else if (a=="--grid" && i+1<argc){ grid=std::max(4, std::atoi(argv[++i])); }
        else if (a=="--extent" && i+3<argc){ half_ext[0]=std::atof(argv[++i]); half_ext[1]=std::atof(argv[++i]); half_ext[2]=std::atof(argv[++i]); }
        else if (a=="--gox" && i+1<argc){ gox_path=argv[++i]; }
        else if (a=="--color" && i+4<argc){ fg[0]=std::atoi(argv[++i]); fg[1]=std::atoi(argv[++i]); fg[2]=std::atoi(argv[++i]); fg[3]=std::atoi(argv[++i]); }
        else { std::fprintf(stderr,"Usage: %s --angles angle.json [--size WxH] [--grid N] [--extent X Y Z] [--gox in.gox] [--color R G B A]\n", argv[0]); return 1; }
    }
    if (!angles_path){ std::fprintf(stderr,"Missing --angles file\n"); return 2; }
    std::vector<View> views; if (!parse_angles_json(angles_path, views)){ std::fprintf(stderr,"Failed to parse angles\n"); return 3; }
    if (gox_path){ float he[3]; if (parse_gox_box_extents(gox_path, he)){ half_ext[0]=he[0]; half_ext[1]=he[1]; half_ext[2]=he[2]; } }

    // Prepare fixed cuboid sample points in [-half_ext, half_ext]^3
    struct Pt { float x,y,z; };
    std::vector<Pt> pts; pts.reserve((size_t)grid*grid*grid);
    for (int k=0;k<grid;k++){
        double z = -half_ext[2] + (2.0*half_ext[2]) * k / (grid-1);
        for (int j=0;j<grid;j++){
            double y = -half_ext[1] + (2.0*half_ext[1]) * j / (grid-1);
            for (int i=0;i<grid;i++){
                double x = -half_ext[0] + (2.0*half_ext[0]) * i / (grid-1);
                // Surface only (reduce points): include if near boundary
                int edge = (i==0||i==grid-1) + (j==0||j==grid-1) + (k==0||k==grid-1);
                if (edge>=1) pts.push_back(Pt{(float)x,(float)y,(float)z});
            }
        }
    }

    auto normalize=[&](float v[3]){ float n=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); if (n>0){ v[0]/=n; v[1]/=n; v[2]/=n; } };
    auto cross=[&](const float a[3], const float b[3], float o[3]){ o[0]=a[1]*b[2]-a[2]*b[1]; o[1]=a[2]*b[0]-a[0]*b[2]; o[2]=a[0]*b[1]-a[1]*b[0]; };

    for (const auto& v : views){
        // camera axes from spherical
        double st=std::sin(v.theta), ct=std::cos(v.theta);
        double sp=std::sin(v.phi), cp=std::cos(v.phi);
        float fwd[3] = { (float)(sp*ct), (float)(sp*st), (float)(cp) };
        float zc[3] = { -fwd[0], -fwd[1], -fwd[2] }; normalize(zc);
        float upw[3] = {0,1,0}; float xc[3]; cross(upw, zc, xc); normalize(xc); float yc[3]; cross(zc, xc, yc); normalize(yc);
        if (v.psi != 0.0){ double c=std::cos(v.psi), s=std::sin(v.psi); float x2[3]={ (float)(c*xc[0] + s*yc[0]), (float)(c*xc[1] + s*yc[1]), (float)(c*xc[2] + s*yc[2]) }; float y2[3]={ (float)(-s*xc[0] + c*yc[0]), (float)(-s*xc[1] + c*yc[1]), (float)(-s*xc[2] + c*yc[2]) }; xc[0]=x2[0]; xc[1]=x2[1]; xc[2]=x2[2]; yc[0]=y2[0]; yc[1]=y2[1]; yc[2]=y2[2]; }

        // project points to 2D
        std::vector<float> xs; std::vector<float> ys; xs.reserve(pts.size()); ys.reserve(pts.size());
        float xmin=1e9f,xmax=-1e9f,ymin=1e9f,ymax=-1e9f;
        for (const auto& p : pts){
            float x = p.x*xc[0] + p.y*xc[1] + p.z*xc[2];
            float y = p.x*yc[0] + p.y*yc[1] + p.z*yc[2];
            xs.push_back(x); ys.push_back(y);
            if (x<xmin) xmin=x; if (x>xmax) xmax=x; if (y<ymin) ymin=y; if (y>ymax) ymax=y;
        }
        float cx = 0.5f*(xmin+xmax); float cy=0.5f*(ymin+ymax);
        float sx = (float)W / std::max(1e-6f, (xmax-xmin));
        float sy = (float)H / std::max(1e-6f, (ymax-ymin));
        float s = 0.9f * std::min(sx, sy) * (float)v.r; // include r scaling

        std::vector<uint8_t> img((size_t)W*H*4, 0);
        auto plot=[&](int px,int py){ if (px<0||py<0||px>=W||py>=H) return; size_t idx=((size_t)py*W + px)*4; img[idx+0]=fg[0]; img[idx+1]=fg[1]; img[idx+2]=fg[2]; img[idx+3]=fg[3]; };
        int thick=2; // simple brush radius
        for (size_t i=0;i<xs.size();i++){
            int u = (int)std::round((xs[i]-cx)*s + (W*0.5f));
            int vpx = (int)std::round((ys[i]-cy)*s + (H*0.5f));
            for (int dy=-thick; dy<=thick; dy++) for (int dx=-thick; dx<=thick; dx++) plot(u+dx, vpx+dy);
        }

        const char* outpath = v.out.empty() ? "out.png" : v.out.c_str();
        if (!write_png_rgba(outpath, W, H, img)) { std::fprintf(stderr, "Failed to write %s\n", outpath); return 4; }
    }
    return 0;
}
