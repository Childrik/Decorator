#include "Decorator.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <map>
#include <unordered_set>

namespace autodecor {
namespace {

constexpr float kPi = 3.14159265358979f;

// ---------------------------------------------------------------- RNG
// Own splitmix64 so the same seed gives the same result on every platform
// (std::uniform_*_distribution is not portable between standard libraries).
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) { next(); next(); }
    uint64_t next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    float u() { return float(next() >> 40) / float(1ull << 24); }   // [0,1)
    float f(float a, float b) { return a + (b - a) * u(); }
    int   i(int a, int b) { return a + int(next() % uint64_t(b - a + 1)); } // inclusive
    bool  chance(float pr) { return u() < pr; }
    template <class T> T pick(std::initializer_list<T> l) { return *(l.begin() + (next() % l.size())); }
};

// ---------------------------------------------------------------- grid helpers
using Mask = std::unordered_set<uint64_t>;
inline uint64_t key(int x, int y) { return (uint64_t(uint32_t(x)) << 32) | uint32_t(y); }
inline int kx(uint64_t k) { return int(uint32_t(k >> 32)); }
inline int ky(uint64_t k) { return int(uint32_t(k & 0xffffffffu)); }
inline bool has(Mask const& m, int x, int y) { return m.count(key(x, y)) != 0; }

enum class Side { Top, Bottom, Left, Right };
// Horizontal side: y = c, x in [a,b].  Vertical side: x = c, y in [a,b].
struct Seg { Side side; float a, b, c; };
inline bool  horiz(Side s) { return s == Side::Top || s == Side::Bottom; }
inline float nrmX(Side s) { return s == Side::Left ? -1.f : s == Side::Right ? 1.f : 0.f; }
inline float nrmY(Side s) { return s == Side::Top ? 1.f : s == Side::Bottom ? -1.f : 0.f; }
inline float nrm(Side s) { return horiz(s) ? nrmY(s) : nrmX(s); }  // sign of outward direction on the "fixed" axis

// Maximal exposed edge runs of a cell mask.
std::vector<Seg> extractEdges(Mask const& m) {
    std::map<int, std::vector<int>> top, bot, left, right;
    for (auto k : m) {
        int x = kx(k), y = ky(k);
        if (!has(m, x, y + 1)) top[y + 1].push_back(x);
        if (!has(m, x, y - 1)) bot[y].push_back(x);
        if (!has(m, x - 1, y)) left[x].push_back(y);
        if (!has(m, x + 1, y)) right[x + 1].push_back(y);
    }
    std::vector<Seg> out;
    auto runs = [&](std::map<int, std::vector<int>>& mp, Side s) {
        for (auto& [c, v] : mp) {
            std::sort(v.begin(), v.end());
            size_t i = 0;
            while (i < v.size()) {
                size_t j = i;
                while (j + 1 < v.size() && v[j + 1] == v[j] + 1) ++j;
                out.push_back({s, v[i] * kCell, (v[j] + 1) * kCell, c * kCell});
                i = j + 1;
            }
        }
    };
    runs(top, Side::Top); runs(bot, Side::Bottom); runs(left, Side::Left); runs(right, Side::Right);
    return out;
}

struct Dash { bool solid; float dmin, dmax, gmin, gmax; };

// ---------------------------------------------------------------- generator
class Gen {
public:
    Gen(std::vector<Cell> const& cells, Params const& prm) : p(prm), rng(prm.seed) {
        int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
        for (auto c : cells) {
            mask.insert(key(c.x, c.y));
            x0 = std::min(x0, c.x); x1 = std::max(x1, c.x);
            y0 = std::min(y0, c.y); y1 = std::max(y1, c.y);
            auto it = cols.find(c.x);
            if (it == cols.end()) cols[c.x] = {c.y, c.y};
            else { it->second.first = std::min(it->second.first, c.y); it->second.second = std::max(it->second.second, c.y); }
        }
        minX = x0 * kCell; minY = y0 * kCell;
        maxX = (x1 + 1) * kCell; maxY = (y1 + 1) * kCell;
        segs = extractEdges(mask);
        maxDepth = std::max(4.f, 0.5f * std::min(maxX - minX, maxY - minY) - 2.f);
    }

    std::vector<Piece> run() {
        Style st = p.style;
        if (st == Style::Chaos) st = rng.pick<Style>({Style::Gate, Style::Circuit, Style::Spiky, Style::Minimal});
        int   d = std::clamp(p.detail, 1, 10);
        float t = std::max(1.f, p.thickness);
        float gap0 = rng.f(5.f, 9.f), gapS = rng.f(5.f, 13.f);
        bool  chaos = p.style == Style::Chaos;

        // Glow goes first so it is created (and drawn) underneath the lines.
        if (p.streaks && st != Style::Minimal) {
            int n = st == Style::Gate ? d * 2 + 4 : st == Style::Spiky ? d + 2 : d / 2 + 2;
            streaks(n, 0.35f);
        }

        switch (st) {
        case Style::Gate:
            if (p.frames && rng.chance(0.5f)) frames(1, false, gap0, gapS, t, 0.f);
            if (p.frames) frames(2 + d / 2 + rng.i(0, 2), true, gap0, gapS, t, 0.2f);
            lintel(t);
            if (p.spires) cornerSpires(t, 1.f);
            if (p.dots)   dotRows(0.55f, 3, true);
            dashRows(t);
            break;
        case Style::Circuit:
            if (p.frames) frames(rng.i(1, 2), true, gap0, gapS, t, 0.3f);
            if (p.frames) frames(rng.i(1, 2), false, gap0, gapS, t, 0.3f);
            traces(3 + d * 2, t);
            if (p.dots) dotRows(0.35f, 2, rng.chance(0.5f));
            break;
        case Style::Spiky:
            if (p.frames) frames(rng.i(1, 2), true, gap0, gapS, t, 0.15f);
            if (p.spires) edgeSpikes(t);
            if (p.dots)   dotRows(0.25f, 2, true);
            break;
        case Style::Minimal:
            if (p.frames) frames(rng.i(1, 3), false, gap0, gapS, t, 0.25f);
            if (p.dots)   dotRows(0.2f, 1, false);
            break;
        case Style::Chaos: break;
        }

        if (chaos) { // sprinkle a few extras on top of the randomly picked base style
            if (rng.chance(0.4f) && p.spires) cornerSpires(t, rng.f(0.6f, 1.4f));
            if (rng.chance(0.4f) && p.frames) frames(rng.i(1, 2), rng.chance(0.5f), gap0, gapS, t, 0.5f);
            if (rng.chance(0.4f)) traces(rng.i(2, 6), t);
        }
        return std::move(out);
    }

private:
    Params p;
    Rng rng;
    Mask mask;
    std::map<int, std::pair<int, int>> cols; // x cell -> (min y cell, max y cell)
    std::vector<Seg> segs;
    std::vector<Piece> out;
    float minX = 0, minY = 0, maxX = 0, maxY = 0, maxDepth = 0;

    // ---- primitives
    bool full() const { return out.size() >= p.maxPieces; }

    void bar(bool h, float fixed, float a, float b, float t, Role r, int fade = 0) {
        if (full() || b - a < 0.8f) return;
        Piece pc; pc.role = r; pc.fade = fade;
        if (h) { pc.x = (a + b) * .5f; pc.y = fixed; pc.w = b - a; pc.h = t; }
        else   { pc.x = fixed; pc.y = (a + b) * .5f; pc.w = t; pc.h = b - a; }
        out.push_back(pc);
    }
    void slant(float x0, float y0, float x1, float y1, float t, Role r) {
        float dx = x1 - x0, dy = y1 - y0, L = std::sqrt(dx * dx + dy * dy);
        if (full() || L < 0.8f) return;
        Piece pc; pc.x = (x0 + x1) * .5f; pc.y = (y0 + y1) * .5f; pc.w = L; pc.h = t; pc.role = r;
        pc.rot = -std::atan2(dy, dx) * 180.f / kPi; // GD rotates clockwise
        out.push_back(pc);
    }
    void dot(float x, float y, float s, Role r = Role::Main) {
        if (full()) return;
        Piece pc; pc.x = x; pc.y = y; pc.w = s; pc.h = s; pc.role = r;
        out.push_back(pc);
    }

    Dash randomDash(float solidChance) {
        if (rng.chance(solidChance)) return {true, 0, 0, 0, 0};
        float d = rng.pick<float>({6.f, 10.f, 16.f, 24.f});
        return {false, d * .6f, d * 1.6f, rng.f(3.f, 8.f), rng.f(6.f, 18.f)};
    }
    void line(bool h, float fixed, float a, float b, float t, Dash const& ds, Role r) {
        if (b - a < 1.f) return;
        if (ds.solid) { bar(h, fixed, a, b, t, r); return; }
        float pos = a;
        while (pos < b - 0.5f && !full()) {
            float e = std::min(b, pos + rng.f(ds.dmin, ds.dmax));
            if (e - pos >= 2.f) bar(h, fixed, pos, e, t, r);
            pos = e + rng.f(ds.gmin, ds.gmax);
        }
    }

    // An exposed edge pushed outward (d>0) or inward (d<0).
    void offsetLine(Seg const& s, float d, float t, Dash const& ds, Role r) {
        float a = s.a - d, b = s.b + d; // grows at convex corners, shrinks when inset
        if (b - a < 2.f) return;
        line(horiz(s.side), s.c + nrm(s.side) * d, a, b, t, ds, r);
    }

    // ---- features
    void frames(int n, bool inward, float g0, float gs, float t0, float accentChance) {
        for (int i = 0; i < n && !full(); ++i) {
            float depth = g0 + gs * i + rng.f(0.f, 2.5f);
            if (inward && depth > maxDepth) break;
            float d = inward ? -depth : depth;
            float t = std::max(1.f, t0 * (1.f - 0.1f * i));
            Dash base = randomDash(0.45f);
            Role r = rng.chance(accentChance) ? Role::Accent : Role::Main;
            for (auto const& s : segs) offsetLine(s, d, t, rng.chance(0.25f) ? randomDash(0.3f) : base, r);
        }
    }

    void lintel(float t0) {
        for (auto const& s : segs) {
            if (s.side != Side::Top || s.b - s.a < 2 * kCell) continue;
            float t = t0 * rng.f(2.2f, 3.6f), ext = t * .5f;
            bar(true, s.c + t * .5f, s.a - ext, s.b + ext, t, Role::Main);
            if (rng.chance(0.6f)) { // short ticks hanging under the lintel
                int n = rng.i(2, 6);
                for (int k = 0; k < n; ++k) {
                    float x = s.a + (s.b - s.a) * rng.f(0.05f, 0.95f);
                    bar(false, x, s.c - rng.f(4.f, 14.f), s.c, t0 * .7f, Role::Main);
                }
            }
        }
    }

    // Triangle drawn in the edge's local frame: u along the edge, v outward.
    void spire(Seg const& s, float u, float lift, float w, float h, bool filled, float t) {
        auto W = [&](float uu, float vv, float& X, float& Y) {
            switch (s.side) {
            case Side::Top:    X = uu;            Y = s.c + vv; break;
            case Side::Bottom: X = uu;            Y = s.c - vv; break;
            case Side::Left:   X = s.c - vv;      Y = uu;       break;
            case Side::Right:  X = s.c + vv;      Y = uu;       break;
            }
        };
        auto L = [&](float u0, float v0, float u1, float v1, float th) {
            float x0 = 0, y0 = 0, x1 = 0, y1 = 0; W(u0, v0, x0, y0); W(u1, v1, x1, y1);
            slant(x0, y0, x1, y1, th, Role::Main);
        };
        float hw = w * .5f;
        L(u - hw, lift, u, lift + h, t);
        L(u + hw, lift, u, lift + h, t);
        L(u - hw - t * .5f, lift, u + hw + t * .5f, lift, t);
        if (filled) { // inner solid triangle as stacked scanlines
            float iw = hw * .45f, ih = h * .55f, iv = lift + h * .12f + t;
            int rows = std::clamp(int(ih / 3.f), 3, 10);
            float rh = ih / rows;
            for (int r = 0; r < rows; ++r) {
                float f0 = float(r) / rows, half = iw * (1.f - f0);
                L(u - half, iv + f0 * ih + rh * .5f, u + half, iv + f0 * ih + rh * .5f, rh * .95f);
            }
        }
        int sparks = rng.i(0, 3);
        for (int k = 0; k < sparks; ++k) {
            float X = 0, Y = 0; W(u + rng.f(-hw * .6f, hw * .6f), lift + h + rng.f(2.f, 12.f), X, Y);
            dot(X, Y, rng.f(1.5f, 3.f));
        }
    }

    void cornerSpires(float t, float scale) {
        for (auto const& s : segs) {
            if (s.side != Side::Top) continue;
            float len = s.b - s.a;
            float w = rng.f(22.f, 46.f) * scale, h = w * rng.f(1.1f, 1.9f);
            if (len < 1.5f * w) { // short ledge: one spire in the middle
                if (rng.chance(0.7f)) spire(s, (s.a + s.b) * .5f, rng.f(1.f, 5.f), std::min(w, len), h, rng.chance(0.7f), t);
                continue;
            }
            for (int end = 0; end < 2; ++end) {
                if (!rng.chance(0.85f)) continue;
                float u = end ? s.b - w * .5f : s.a + w * .5f;
                spire(s, u, rng.f(1.f, 5.f), w, h, rng.chance(0.7f), t);
            }
        }
    }

    void edgeSpikes(float t) {
        for (auto const& s : segs) {
            if (s.side == Side::Bottom && rng.chance(0.5f)) continue;
            float sp = rng.f(14.f, 30.f), h = sp * rng.f(1.f, 1.8f);
            for (float u = s.a + sp * .5f; u < s.b - sp * .4f && !full(); u += sp)
                spire(s, u, rng.f(1.f, 3.f), sp * .8f, h, false, std::max(1.f, t * .8f));
        }
    }

    void dotRows(float prob, int maxRows, bool inward) {
        for (auto const& s : segs) {
            if (!rng.chance(prob)) continue;
            int rows = rng.i(1, maxRows);
            for (int r = 0; r < rows; ++r) {
                float depth = rng.f(4.f, 14.f) + r * rng.f(3.f, 6.f);
                if (inward && depth > maxDepth) continue;
                float d = inward ? -depth : depth;
                float size = rng.pick<float>({1.5f, 2.f, 2.5f, 3.f});
                float spacing = rng.pick<float>({7.f, 9.f, 12.f, 16.f});
                float c = s.c + nrm(s.side) * d;
                float pos = s.a + rng.f(2.f, spacing), end = s.b - rng.f(0.f, 8.f);
                while (pos < end && !full()) {
                    if (horiz(s.side)) dot(pos, c, size); else dot(c, pos, size);
                    pos += spacing * (rng.chance(0.22f) ? 2.f : 1.f);
                }
            }
        }
    }

    void dashRows(float t) {
        for (auto const& s : segs) {
            if (!horiz(s.side) || s.b - s.a < kCell) continue;
            if (s.side == Side::Bottom && rng.chance(0.6f)) continue;
            int rows = rng.i(1, 3);
            float len = s.b - s.a;
            for (int r = 0; r < rows; ++r) {
                float y = s.c + nrm(s.side) * -(6.f + r * 5.f + rng.f(0.f, 3.f));
                float pos = s.a + len * rng.f(0.1f, 0.4f), end = s.b - len * rng.f(0.1f, 0.4f);
                while (pos < end && !full()) {
                    float l = rng.pick<float>({4.f, 8.f, 14.f, 22.f, 34.f});
                    bar(true, y, pos, std::min(pos + l, end), std::max(1.f, t * .8f), Role::Main);
                    pos += l + rng.f(4.f, 16.f);
                }
            }
        }
    }

    // Vertical light streaks that fade away from a random brightest point.
    void streaks(int n, float budgetFrac) {
        size_t cap = size_t(p.maxPieces * budgetFrac);
        int steps = std::max(1, p.fadeSteps), K = steps * 2;
        for (int i = 0; i < n && out.size() < cap; ++i) {
            float x = rng.f(minX + 6.f, maxX - 6.f);
            if (cols.find(int(std::floor(x / kCell))) == cols.end()) continue;
            // Use the whole bounding height (not just this column's cells) so glow also fills openings.
            float cb = minY, ch = maxY - minY;
            float yBot = cb + ch * rng.f(0.f, 0.15f), yTop = cb + ch * rng.f(0.45f, 0.95f);
            float w = rng.pick<float>({1.5f, 2.f, 3.f, 3.f, 5.f, 8.f, 12.f});
            int peak = rng.i(0, K - 1);
            float sh = (yTop - yBot) / K;
            if (sh < 2.f) continue;
            for (int k = 0; k < K; ++k) {
                int fade = std::min(steps - 1, std::abs(k - peak) / 2);
                bar(false, x, yBot + k * sh - .4f, yBot + (k + 1) * sh + .4f, w, Role::Glow, fade);
            }
            if (w >= 3.f && rng.chance(0.5f)) // bright core near the peak
                bar(false, x, yBot + std::max(0, peak - 2) * sh, yBot + std::min(K, peak + 3) * sh, 1.5f, Role::Main);
        }
    }

    // Right-angle traces growing out of the edges, ending in a small node.
    void traces(int n, float t0) {
        if (segs.empty()) return;
        float t = std::max(1.f, t0 * .8f);
        for (int i = 0; i < n && !full(); ++i) {
            Seg const& s = segs[size_t(rng.i(0, int(segs.size()) - 1))];
            float u = s.a + (s.b - s.a) * rng.f(0.1f, 0.9f);
            float x = horiz(s.side) ? u : s.c, y = horiz(s.side) ? s.c : u;
            float dx = nrmX(s.side), dy = nrmY(s.side);
            Role r = rng.chance(0.3f) ? Role::Accent : Role::Main;
            int steps = rng.i(2, 3);
            for (int k = 0; k < steps; ++k) {
                float len = rng.pick<float>({20.f, 30.f, 45.f, 60.f});
                float x2 = x + dx * len, y2 = y + dy * len;
                slant(x, y, x2, y2, t, r);
                dot(x2, y2, t, r); // fills the corner
                x = x2; y = y2;
                float sg = rng.chance(0.5f) ? 1.f : -1.f, ndx = -dy * sg, ndy = dx * sg;
                if (ndx * nrmX(s.side) + ndy * nrmY(s.side) < -0.5f) { ndx = -ndx; ndy = -ndy; }
                dx = ndx; dy = ndy;
            }
            float ns = rng.f(6.f, 9.f); // node: square ring + center dot
            bar(true, y + ns * .5f, x - ns * .5f, x + ns * .5f, t, r);
            bar(true, y - ns * .5f, x - ns * .5f, x + ns * .5f, t, r);
            bar(false, x - ns * .5f, y - ns * .5f, y + ns * .5f, t, r);
            bar(false, x + ns * .5f, y - ns * .5f, y + ns * .5f, t, r);
            dot(x, y, t * 1.2f, r);
        }
    }
};

} // namespace

Style styleFromString(std::string const& s) {
    std::string l; for (char c : s) l += char(std::tolower((unsigned char)c));
    if (l == "circuit") return Style::Circuit;
    if (l == "spiky")   return Style::Spiky;
    if (l == "minimal") return Style::Minimal;
    if (l == "chaos")   return Style::Chaos;
    return Style::Gate;
}

std::vector<Piece> generate(std::vector<Cell> const& cells, Params const& p) {
    if (cells.empty()) return {};
    return Gen(cells, p).run();
}

} // namespace autodecor
