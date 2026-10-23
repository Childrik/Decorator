#include "Decorator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <unordered_set>

namespace autodecor {
namespace {

constexpr float PI = 3.14159265358979323846f;

struct RNG {
    uint64_t state;

    explicit RNG(uint64_t seed)
        : state(seed + 0x9E3779B97F4A7C15ull) {}

    uint64_t next() {
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    float f(float a, float b) {
        return a + (b - a) *
            (float(next() >> 40) / 16777216.0f);
    }

    int i(int a, int b) {
        return a + int(next() % uint64_t(b - a + 1));
    }
};

uint64_t cellKey(int x, int y) {
    return (uint64_t(uint32_t(x)) << 32) | uint32_t(y);
}

struct Edge {
    bool horizontal;
    float a;
    float b;
    float fixed;
    int side;
};

class Generator {
public:
    Generator(std::vector<Cell> const& cells, Params const& params)
        : p(params), rng(params.seed) {
        for (auto const& c : cells) {
            mask.insert(cellKey(c.x, c.y));
        }

        if (mask.empty()) return;

        int minCX = INT32_MAX;
        int minCY = INT32_MAX;
        int maxCX = INT32_MIN;
        int maxCY = INT32_MIN;

        for (auto const& c : cells) {
            minCX = std::min(minCX, c.x);
            minCY = std::min(minCY, c.y);
            maxCX = std::max(maxCX, c.x);
            maxCY = std::max(maxCY, c.y);
        }

        left = minCX * kCell;
        bottom = minCY * kCell;
        right = (maxCX + 1) * kCell;
        top = (maxCY + 1) * kCell;

        extractEdges();
    }

    std::vector<Piece> run() {
        if (mask.empty()) return {};

        int detail = std::clamp(p.detail, 1, 10);
        float thickness = std::clamp(p.thickness, 1.f, 8.f);

        // Keep the main contour clean.
        for (auto const& e : edges) {
            line(e, 0.f, thickness, Role::Main);
        }

        // Nested frames.
        if (p.frames) {
            int count = 1 + detail / 3;

            for (int i = 1; i <= count && !full(); ++i) {
                float offset = 5.f + i * (5.f + thickness);

                for (auto const& e : edges) {
                    if (p.style == Style::Minimal) break;

                    float d = offset;
                    if (e.side == 0 || e.side == 2) {
                        // Horizontal edges.
                        line(e, d, thickness * .7f,
                            roleForStyle());
                    } else {
                        line(e, d, thickness * .7f,
                            roleForStyle());
                    }
                }
            }
        }

        addStyleDetails(detail, thickness);

        return std::move(out);
    }

private:
    Params p;
    RNG rng;
    std::unordered_set<uint64_t> mask;
    std::vector<Edge> edges;
    std::vector<Piece> out;

    float left = 0.f;
    float bottom = 0.f;
    float right = 0.f;
    float top = 0.f;

    bool full() const {
        return out.size() >= p.maxPieces;
    }

    bool has(int x, int y) const {
        return mask.count(cellKey(x, y)) != 0;
    }

    void extractEdges() {
        for (auto const& k : mask) {
            int x = int(uint32_t(k >> 32));
            int y = int(uint32_t(k));

            float x0 = x * kCell;
            float x1 = x0 + kCell;
            float y0 = y * kCell;
            float y1 = y0 + kCell;

            if (!has(x, y + 1))
                edges.push_back({true, x0, x1, y1, 0});

            if (!has(x, y - 1))
                edges.push_back({true, x0, x1, y0, 1});

            if (!has(x - 1, y))
                edges.push_back({false, y0, y1, x0, 2});

            if (!has(x + 1, y))
                edges.push_back({false, y0, y1, x1, 3});
        }

        // Merge adjacent edges with the same direction and position.
        std::sort(edges.begin(), edges.end(),
            [](Edge const& a, Edge const& b) {
                if (a.horizontal != b.horizontal)
                    return a.horizontal > b.horizontal;
                if (a.side != b.side)
                    return a.side < b.side;
                if (a.fixed != b.fixed)
                    return a.fixed < b.fixed;
                return a.a < b.a;
            });

        std::vector<Edge> merged;

        for (auto const& e : edges) {
            if (!merged.empty()) {
                auto& last = merged.back();

                if (last.horizontal == e.horizontal &&
                    last.side == e.side &&
                    std::abs(last.fixed - e.fixed) < .01f &&
                    e.a <= last.b + .01f) {
                    last.b = std::max(last.b, e.b);
                    continue;
                }
            }

            merged.push_back(e);
        }

        edges = std::move(merged);
    }

    Role roleForStyle() const {
        switch (p.style) {
            case Style::PurpleGlow:
            case Style::Cyberpunk:
            case Style::Rainbow:
                return Role::Accent;

            case Style::Space:
            case Style::Futuristic:
                return Role::Glow;

            default:
                return Role::Main;
        }
    }

    void addBar(bool horizontal, float fixed,
                float a, float b, float t, Role role) {
        if (full() || b - a < 2.f) return;

        Piece pc;
        pc.role = role;
        pc.w = horizontal ? b - a : t;
        pc.h = horizontal ? t : b - a;
        pc.x = horizontal ? (a + b) * .5f : fixed;
        pc.y = horizontal ? fixed : (a + b) * .5f;

        out.push_back(pc);
    }

    void line(Edge const& e, float offset,
              float thickness, Role role) {
        float a = e.a - offset;
        float b = e.b + offset;

        float fixed = e.fixed;

        // Push the contour outward from the selected shape.
        if (e.side == 0) fixed += offset;
        if (e.side == 1) fixed -= offset;
        if (e.side == 2) fixed -= offset;
        if (e.side == 3) fixed += offset;

        addBar(e.horizontal, fixed, a, b, thickness, role);
    }

    void addDot(float x, float y, float size, Role role) {
        if (full()) return;

        Piece pc;
        pc.x = x;
        pc.y = y;
        pc.w = size;
        pc.h = size;
        pc.role = role;

        out.push_back(pc);
    }

    void addStyleDetails(int detail, float thickness) {
        if (p.style == Style::Minimal) return;

        // Accent markers at the corners.
        if (p.dots) {
            for (auto const& e : edges) {
                if (full()) break;

                float size = std::max(2.f, thickness * 1.4f);

                addDot(e.a, e.fixed, size, roleForStyle());
                addDot(e.b, e.fixed, size, roleForStyle());
            }
        }

        // Clean inner horizontal lines.
        if (p.style == Style::Monochrome ||
            p.style == Style::Futuristic ||
            p.style == Style::Cyberpunk ||
            p.style == Style::PurpleGlow ||
            p.style == Style::DarkAtmosphere) {

            for (int i = 1; i <= detail && !full(); ++i) {
                float y = bottom +
                    (top - bottom) * float(i) / float(detail + 1);

                addBar(true, y, left + 8.f, right - 8.f,
                    thickness, roleForStyle());
            }
        }

        // Small orbital markers.
        if (p.style == Style::Space) {
            int count = std::min(20, 3 + detail * 2);

            for (int i = 0; i < count && !full(); ++i) {
                float x = rng.f(left + 8.f, right - 8.f);
                float y = rng.f(bottom + 8.f, top - 8.f);

                addDot(x, y, rng.f(2.f, 4.f), Role::Accent);
            }
        }

        // Nature-inspired angular branches.
        if (p.style == Style::Nature) {
            for (int i = 0; i < detail && !full(); ++i) {
                float x = rng.f(left + 10.f, right - 10.f);
                float y = rng.f(bottom + 10.f, top - 10.f);
                float len = rng.f(8.f, 18.f);

                addBar(true, y, x, x + len,
                    thickness, Role::Accent);
            }
        }

        // Rainbow uses alternating color roles.
        if (p.style == Style::Rainbow) {
            for (size_t i = 0; i < out.size(); ++i) {
                if (i % 3 == 0) out[i].role = Role::Main;
                else if (i % 3 == 1) out[i].role = Role::Accent;
                else out[i].role = Role::Glow;
            }
        }

        // Hell style: angular accents, but no spikes.
        if (p.style == Style::Hell) {
            for (auto const& e : edges) {
                if (full()) break;

                float mid = (e.a + e.b) * .5f;
                float len = std::min(12.f, (e.b - e.a) * .2f);

                addBar(e.horizontal, e.fixed,
                    mid - len, mid + len,
                    thickness * 1.5f, Role::Accent);
            }
        }
    }
};

} // namespace

Style styleFromString(std::string const& s) {
    if (s == "monochrome" || s == "gate")
        return Style::Monochrome;
    if (s == "purpleglow" || s == "purple-glow")
        return Style::PurpleGlow;
    if (s == "cyberpunk" || s == "circuit")
        return Style::Cyberpunk;
    if (s == "hell" || s == "spiky")
        return Style::Hell;
    if (s == "minimal")
        return Style::Minimal;
    if (s == "futuristic")
        return Style::Futuristic;
    if (s == "space")
        return Style::Space;
    if (s == "nature")
        return Style::Nature;
    if (s == "rainbow")
        return Style::Rainbow;
    if (s == "darkatmosphere" || s == "dark-atmosphere" ||
        s == "chaos")
        return Style::DarkAtmosphere;

    return Style::Monochrome;
}

std::vector<Piece> generate(
    std::vector<Cell> const& cells,
    Params const& p
) {
    if (cells.empty()) return {};
    return Generator(cells, p).run();
}

} // namespace autodecor
