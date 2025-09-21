#include "physics.h"

#include <algorithm>
#include <cmath>

namespace physics {

float
get_collision_time (const std::vector<PhysicsBody> &bodies,
                    float time_horizon,
                    float minx, float miny, float maxx, float maxy)
{
    if (time_horizon <= 0.0f) return -1.0f;
    struct K { double px, py, vx, vy, ax, ay, R; };
    std::vector<K> list; list.reserve(bodies.size());
    for (const auto& b : bodies) {
        if (!(b.px >= minx && b.px <= maxx && b.py >= miny && b.py <= maxy)) continue;
        double ax = 0.0, ay = 0.0;
        if (b.throttle) {
            ax = (double)PHYS_ACCEL_PX_S2 * std::cos(b.theta);
            ay = (double)PHYS_ACCEL_PX_S2 * std::sin(b.theta);
        }
        list.push_back({ b.px, b.py, b.vx, b.vy, ax, ay, b.radius });
    }
    if (list.size() < 2) return -1.0f;

    auto dist2 = [](double dx, double dy){ return dx*dx + dy*dy; };
    const int samples = 200;
    float best_t = -1.0f;
    for (size_t i = 0; i < list.size(); ++i) {
        for (size_t j = i + 1; j < list.size(); ++j) {
            const K& A = list[i];
            const K& B = list[j];
            double p0x = A.px - B.px;
            double p0y = A.py - B.py;
            double v0x = A.vx - B.vx;
            double v0y = A.vy - B.vy;
            double ax  = A.ax - B.ax;
            double ay  = A.ay - B.ay;
            double RR = (A.R + B.R);
            RR = RR * RR;
            if (dist2(p0x, p0y) <= RR) { if (best_t < 0.0f || 0.0f < best_t) best_t = 0.0f; continue; }

            double prev_t = 0.0;
            double prev_dx = p0x, prev_dy = p0y;
            double prev_d2 = dist2(prev_dx, prev_dy);
            for (int sidx = 1; sidx <= samples; ++sidx) {
                double t = (double)sidx * (double)time_horizon / (double)samples;
                double dx = p0x + v0x * t + 0.5 * ax * t * t;
                double dy = p0y + v0y * t + 0.5 * ay * t * t;
                double d2 = dist2(dx, dy);
                if (d2 <= RR || (prev_d2 > RR && d2 < prev_d2)) {
                    double a = prev_t, b = t;
                    for (int it = 0; it < 24; ++it) {
                        double m = 0.5 * (a + b);
                        double mdx = p0x + v0x * m + 0.5 * ax * m * m;
                        double mdy = p0y + v0y * m + 0.5 * ay * m * m;
                        double md2 = dist2(mdx, mdy);
                        if (md2 > RR) a = m; else b = m;
                    }
                    float t_hit = (float)b;
                    if (t_hit >= 0.0f && t_hit <= time_horizon) {
                        if (best_t < 0.0f || t_hit < best_t) best_t = t_hit;
                    }
                    break;
                }
                prev_t = t; prev_dx = dx; prev_dy = dy; prev_d2 = d2;
            }
        }
    }
    return best_t;
}

std::vector<DebrisSpawn>
compute_debris_for (const Object &obj, int team, std::mt19937 &rng)
{
    // 10 debris2, 2 debris1, 1 debris3
    struct Req { const char* key; int count; } reqs[] = { {"debris2", 10}, {"debris1", 2}, {"debris3", 1} };
    std::normal_distribution<double> nboost(0.0, 300.0);
    std::normal_distribution<double> nspin(0.0, 1.0);
    std::uniform_real_distribution<double> utheta(0.0, 2.0*M_PI);
    double sx = obj.x_pixels();
    double sy = obj.y_pixels();
    double svx = (double)obj.vx / (double)Object::FP_ONE;
    double svy = (double)obj.vy / (double)Object::FP_ONE;
    std::vector<DebrisSpawn> out;
    for (auto r : reqs) {
        for (int i = 0; i < r.count; ++i) {
            double theta = utheta(rng);
            double mag = std::fabs(nboost(rng));
            double dvx = mag * std::cos(theta);
            double dvy = mag * std::sin(theta);
            DebrisSpawn d;
            d.key = r.key;
            d.x = sx; d.y = sy;
            d.vx = svx + dvx; d.vy = svy + dvy;
            d.ang_vel = nspin(rng);
            d.team = team;
            out.push_back(d);
        }
    }
    return out;
}

} // namespace physics
