// Inject or append an active camera into a .gox file.
// Modes:
//  - Vector mode: provide --eye/--center/--up
//  - JSON spherical mode: provide --angles angles.json with objects {out,r,theta,phi,psi}
// In JSON mode, we try to scale distance so the model likely fits in view
// using the 'box' entry from the GOX IMG chunk if available, else defaults.
// Optionally export PNG by invoking the goxel binary.
//
// Build:
//   g++ -std=c++17 -O2 gox_set_camera.cpp -o gox_set_camera
//
// Examples:
//  1) Vector mode, write gox and export:
//     ./gox_set_camera --eye 0 0 128 --center 0 0 0 --up 0 1 0 \
//         --export out.png --goxel /path/to/goxel in.gox tmp.gox
//  2) JSON spherical mode (angle.json is an array of objects):
//     ./gox_set_camera --angles angle.json --goxel /path/to/goxel \
//         --fov-deg 45 in.gox tmp.gox
//     Each object may contain: {"out":"view.png","r":1.0,"theta":0.0,"phi":1.0,"psi":0.0}

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>

static void die(const char* msg) { std::fprintf(stderr, "%s\n", msg); std::exit(1); }

static bool read_all(const char* path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path, "rb"); if (!f) return false;
    std::vector<uint8_t> buf(1<<16);
    while (true) {
        size_t r = std::fread(buf.data(), 1, buf.size(), f);
        if (r) out.insert(out.end(), buf.begin(), buf.begin() + r);
        if (r < buf.size()) break;
    }
    std::fclose(f); return true;
}

static bool write_all(const char* path, const std::vector<uint8_t>& data) {
    FILE* f = std::fopen(path, "wb"); if (!f) return false;
    size_t w = std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f); return w == data.size();
}

static void le32(uint32_t v, std::vector<uint8_t>& out) {
    out.push_back((uint8_t)(v & 0xFF));
    out.push_back((uint8_t)((v >> 8) & 0xFF));
    out.push_back((uint8_t)((v >> 16) & 0xFF));
    out.push_back((uint8_t)((v >> 24) & 0xFF));
}

static void append_dict_entry(const std::string& key, const void* data, uint32_t size, std::vector<uint8_t>& out) {
    le32((uint32_t)key.size(), out);
    out.insert(out.end(), key.begin(), key.end());
    le32(size, out);
    if (size) out.insert(out.end(), (const uint8_t*)data, (const uint8_t*)data + size);
}

static void normalize(float v[3]) {
    float n = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (n == 0) return; v[0]/=n; v[1]/=n; v[2]/=n;
}

static void cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1]*b[2] - a[2]*b[1];
    out[1] = a[2]*b[0] - a[0]*b[2];
    out[2] = a[0]*b[1] - a[1]*b[0];
}

// Build camera->mat directly (world transform of camera), given eye, center, up.
// Goxel expects the camera to look along -Z in camera space, so z_axis points toward the viewer.
static void build_camera_mat(const float eye[3], const float center[3], const float up_in[3], float mat[4][4]) {
    float f[3] = {center[0]-eye[0], center[1]-eye[1], center[2]-eye[2]}; // forward
    normalize(f);
    float z_axis[3] = {-f[0], -f[1], -f[2]};                    // camera +Z points towards viewer, so -fwd
    float x_axis[3];
    float up[3] = {up_in[0], up_in[1], up_in[2]}; normalize(up);
    cross(up, z_axis, x_axis); normalize(x_axis);
    float y_axis[3];
    cross(z_axis, x_axis, y_axis); normalize(y_axis);
    // Fill as row-major with last row translation like goxel uses (mat[3] is translation).
    // Rows contain basis axes components.
    mat[0][0] = x_axis[0]; mat[0][1] = x_axis[1]; mat[0][2] = x_axis[2]; mat[0][3] = 0;
    mat[1][0] = y_axis[0]; mat[1][1] = y_axis[1]; mat[1][2] = y_axis[2]; mat[1][3] = 0;
    mat[2][0] = z_axis[0]; mat[2][1] = z_axis[1]; mat[2][2] = z_axis[2]; mat[2][3] = 0;
    mat[3][0] = eye[0];    mat[3][1] = eye[1];    mat[3][2] = eye[2];    mat[3][3] = 1;
}

// Attempt to parse the GOX IMG 'box' entry for half-extents and (approx) center.
// Returns true on success, fills half_extents (>=0) and center.
static bool parse_gox_box(const std::vector<uint8_t>& file, float half_extents[3], float center[3]) {
    // naive search for pattern: 03 00 00 00 'b' 'o' 'x' <size:le32> <data>
    auto rd_le32 = [&](const uint8_t* p){ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); };
    for (size_t i = 0; i + 7 + 4 <= file.size(); i++) {
        if (file[i]==3 && file[i+1]==0 && file[i+2]==0 && file[i+3]==0 &&
            file[i+4]=='b' && file[i+5]=='o' && file[i+6]=='x') {
            size_t p = i + 7; if (p + 4 > file.size()) break; uint32_t sz = rd_le32(&file[p]); p += 4;
            if (p + sz > file.size()) break;
            if (sz == 24) {
                // min3, max3 floats
                const float* fp = reinterpret_cast<const float*>(&file[p]);
                float minv[3] = {fp[0], fp[1], fp[2]};
                float maxv[3] = {fp[3], fp[4], fp[5]};
                for (int k=0;k<3;k++) {
                    center[k] = 0.5f*(minv[k] + maxv[k]);
                    half_extents[k] = 0.5f*std::fabs(maxv[k] - minv[k]);
                }
                return true;
            } else if (sz == 64) {
                // 4x4 floats (row-major). Diagonal elements represent extents in many GOX files.
                const float* fp = reinterpret_cast<const float*>(&file[p]);
                // half sizes from diagonal magnitudes
                half_extents[0] = std::fabs(fp[0]);
                half_extents[1] = std::fabs(fp[5]);
                half_extents[2] = std::fabs(fp[10]);
                // center best-effort: last row first 3 (row-major) sometimes stores translation; else zero.
                center[0] = fp[12]; center[1] = fp[13]; center[2] = fp[14];
                return true;
            } else {
                // Unknown format; fallback
                break;
            }
        }
    }
    return false;
}

struct SphericalView { std::string out; double r=1.0, theta=0.0, phi=0.0, psi=0.0; };

// Minimal, lenient parser for angle.json arrays with objects containing out/r/theta/phi/psi.
static bool parse_angles_json(const char* path, std::vector<SphericalView>& views) {
    std::vector<uint8_t> buf; if (!read_all(path, buf)) return false;
    std::string s(reinterpret_cast<const char*>(buf.data()), buf.size());
    size_t pos = 0;
    while (true) {
        size_t l = s.find('{', pos); if (l == std::string::npos) break;
        size_t r = s.find('}', l + 1); if (r == std::string::npos) break;
        std::string obj = s.substr(l+1, r-l-1);
        SphericalView v;
        auto find_num = [&](const char* key, double def)->double{
            size_t k = obj.find(key); if (k == std::string::npos) return def;
            k = obj.find(':', k); if (k == std::string::npos) return def; k++;
            // skip spaces
            while (k < obj.size() && std::isspace((unsigned char)obj[k])) k++;
            char* endp = nullptr;
            double val = std::strtod(obj.c_str() + k, &endp);
            return val;
        };
        auto find_str = [&](const char* key)->std::string{
            size_t k = obj.find(key); if (k == std::string::npos) return std::string();
            k = obj.find(':', k); if (k == std::string::npos) return std::string();
            k = obj.find('"', k); if (k == std::string::npos) return std::string();
            size_t e = obj.find('"', k+1); if (e == std::string::npos) return std::string();
            return obj.substr(k+1, e-(k+1));
        };
        v.out = find_str("out");
        v.r = find_num("\"r\"", 1.0);
        v.theta = find_num("\"theta\"", 0.0);
        v.phi = find_num("\"phi\"", 0.0);
        v.psi = find_num("\"psi\"", 0.0);
        views.push_back(v);
        pos = r + 1;
    }
    return !views.empty();
}

static void rotate_roll_about_forward(const float z_axis[3], float x_axis[3], float y_axis[3], double psi) {
    // Rotate x/y around z by angle psi
    double c = std::cos(psi), s = std::sin(psi);
    float xr[3] = { (float)(c*x_axis[0] + s*y_axis[0]), (float)(c*x_axis[1] + s*y_axis[1]), (float)(c*x_axis[2] + s*y_axis[2]) };
    float yr[3] = { (float)(-s*x_axis[0] + c*y_axis[0]), (float)(-s*x_axis[1] + c*y_axis[1]), (float)(-s*x_axis[2] + c*y_axis[2]) };
    x_axis[0]=xr[0]; x_axis[1]=xr[1]; x_axis[2]=xr[2];
    y_axis[0]=yr[0]; y_axis[1]=yr[1]; y_axis[2]=yr[2];
}

int main(int argc, char** argv) {
    // Parse args
    if (argc < 2) {
        std::fprintf(stderr,
            "Usage:\n  %s --eye EX EY EZ --center CX CY CZ --up UX UY UZ in.gox out.gox [--export out.png] [--goxel BIN]\n  %s --angles angles.json in.gox out.gox [--goxel BIN] [--fov-deg D]\n",
            argv[0]);
        return 1;
    }
    float eye[3] = {NAN,NAN,NAN}, center[3] = {NAN,NAN,NAN}, up[3] = {NAN,NAN,NAN};
    const char* angles_json = nullptr;
    double fov_deg = 45.0; // used only in JSON spherical mode
    const char* in_path = nullptr; const char* out_path = nullptr;
    const char* export_png = nullptr; const char* goxel_bin = std::getenv("GOXEL_BIN");
    for (int i=1;i<argc;i++) {
        std::string a = argv[i];
        auto need3 = [&](float v[3]){
            if (i+3 >= argc) die("Missing vector components");
            v[0] = std::atof(argv[++i]); v[1] = std::atof(argv[++i]); v[2] = std::atof(argv[++i]);
        };
        if (a == std::string("--eye")) need3(eye);
        else if (a == std::string("--center")) need3(center);
        else if (a == std::string("--up")) need3(up);
        else if (a == std::string("--export")) { if (++i>=argc) die("--export needs path"); export_png = argv[i]; }
        else if (a == std::string("--goxel")) { if (++i>=argc) die("--goxel needs path"); goxel_bin = argv[i]; }
        else if (a == std::string("--angles")) { if (++i>=argc) die("--angles needs path"); angles_json = argv[i]; }
        else if (a == std::string("--fov-deg")) { if (++i>=argc) die("--fov-deg needs value"); fov_deg = std::atof(argv[i]); }
        else if (!in_path) in_path = argv[i];
        else if (!out_path) out_path = argv[i];
        else die("Too many positional args");
    }
    if (!in_path || !out_path) die("Need input and output .gox paths");

    std::vector<uint8_t> file;
    if (!read_all(in_path, file)) die("Cannot read input file");
    if (file.size() < 8 || std::memcmp(file.data(), "GOX ", 4) != 0) die("Input is not a .gox file");

    bool transparent_bg = true; // default to make PNG background transparent when exporting
    auto make_png_background_transparent = [&](const char* png_path){
        if (!png_path) return;
        // Try ImageMagick 'convert' or 'magick' to set top-left color transparent
        std::string quoted = std::string("\"") + png_path + "\"";
        std::string cmd =
            "sh -lc '"
            "if command -v convert >/dev/null 2>&1; then "
              "color=$(convert " + quoted + " -format \"%[pixel:p{0,0}]\" info:); "
              "convert " + quoted + " -alpha on -transparent \"$color\" " + quoted + "; "
            "elif command -v magick >/dev/null 2>&1; then "
              "color=$(magick " + quoted + " -format \"%[pixel:p{0,0}]\" info:); "
              "magick " + quoted + " -alpha on -transparent \"$color\" " + quoted + "; "
            "else "
              "echo \"Note: ImageMagick not found; PNG transparency post-process skipped.\" 1>&2; "
            "fi'";
        std::system(cmd.c_str());
    };

    auto write_with_camera_and_maybe_export = [&](const float eye_in[3], const float center_in[3], const float up_in[3], const char* export_path)->int{
        // Build CAMR chunk payload
        float mat[4][4];
        build_camera_mat(eye_in, center_in, up_in, mat);
        float dist = std::sqrt((center_in[0]-eye_in[0])*(center_in[0]-eye_in[0]) +
                               (center_in[1]-eye_in[1])*(center_in[1]-eye_in[1]) +
                               (center_in[2]-eye_in[2])*(center_in[2]-eye_in[2]));
        uint8_t ortho = 0;

        std::vector<uint8_t> dict;
        const char* name = "cli";
        append_dict_entry("name", name, (uint32_t)std::strlen(name), dict);
        append_dict_entry("dist", &dist, sizeof(dist), dict);
        append_dict_entry("ortho", &ortho, sizeof(ortho), dict);
        append_dict_entry("mat", mat, sizeof(mat), dict);
        append_dict_entry("active", nullptr, 0, dict);

        // Build new file: original bytes + CAMR chunk
        std::vector<uint8_t> out = file;
        out.insert(out.end(), {'C','A','M','R'});
        le32((uint32_t)dict.size(), out);
        out.insert(out.end(), dict.begin(), dict.end());
        le32(0, out); // CRC placeholder

        if (!write_all(out_path, out)) die("Cannot write output file");

        if (export_path) {
            if (!goxel_bin) die("Provide --goxel or set GOXEL_BIN to goxel binary path");
            std::string cmd = std::string("\"") + goxel_bin + "\" -e \"" + export_path + "\" \"" + out_path + "\"";
            int rc = std::system(cmd.c_str());
            if (rc != 0) {
                std::fprintf(stderr, "goxel export failed with code %d\n", rc);
                return rc ? rc : 1;
            }
            if (transparent_bg) make_png_background_transparent(export_path);
        }
        return 0;
    };

    if (angles_json) {
        // JSON spherical mode
        std::vector<SphericalView> views;
        if (!parse_angles_json(angles_json, views)) die("Failed to parse --angles JSON (expects array of objects)");

        // Try to infer half-extents and center from 'box'
        float half_ext[3] = {64.0f,64.0f,64.0f};
        float auto_center[3] = {0.0f,0.0f,0.0f};
        if (!parse_gox_box(file, half_ext, auto_center)) {
            // fallback guess
            auto_center[0]=0; auto_center[1]=0; auto_center[2]=0;
            half_ext[0]=half_ext[1]=half_ext[2]=64.0f;
        }
        double fov = fov_deg * M_PI / 180.0;
        double radius_sphere = std::sqrt((double)half_ext[0]*half_ext[0] + (double)half_ext[1]*half_ext[1] + (double)half_ext[2]*half_ext[2]);
        double base_dist = radius_sphere / std::max(1e-6, std::sin(fov * 0.5)) * 1.05; // margin 5%

        int rc_total = 0;
        for (const auto& v : views) {
            // Forward unit from spherical (physics convention):
            double st = std::sin(v.theta), ct = std::cos(v.theta);
            double sp = std::sin(v.phi), cp = std::cos(v.phi);
            float forward[3] = { (float)(sp*ct), (float)(sp*st), (float)(cp) };
            // Eye at center - forward * distance
            double dist = base_dist * v.r;
            float e[3] = { (float)(auto_center[0] - forward[0]*dist),
                           (float)(auto_center[1] - forward[1]*dist),
                           (float)(auto_center[2] - forward[2]*dist) };

            // Build up vector with roll psi
            // Start with world up
            float z_axis[3] = { -forward[0], -forward[1], -forward[2] }; // camera z towards viewer
            normalize(z_axis);
            float world_up[3] = {0,1,0};
            float x_axis[3]; cross(world_up, z_axis, x_axis); normalize(x_axis);
            float y_axis[3]; cross(z_axis, x_axis, y_axis); normalize(y_axis);
            rotate_roll_about_forward(z_axis, x_axis, y_axis, v.psi);
            float u[3] = { y_axis[0], y_axis[1], y_axis[2] };

            int rc = write_with_camera_and_maybe_export(e, auto_center, u, v.out.empty() ? nullptr : v.out.c_str());
            if (rc_total == 0 && rc != 0) rc_total = rc;
        }
        return rc_total;
    }

    // Vector mode (legacy)
    for (int k=0;k<3;k++) if (std::isnan(eye[k])||std::isnan(center[k])||std::isnan(up[k])) die("Missing --eye/--center/--up");
    return write_with_camera_and_maybe_export(eye, center, up, export_png);
}
