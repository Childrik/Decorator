#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace autodecor {

constexpr float kCell = 30.f;

struct Cell {
    int x, y;
};

// Ten decoration styles.
enum class Style {
    Monochrome,
    PurpleGlow,
    Cyberpunk,
    Hell,
    Minimal,
    Futuristic,
    Space,
    Nature,
    Rainbow,
    DarkAtmosphere,

    // Legacy style aliases for compatibility.
    Gate = Monochrome,
    Circuit = Cyberpunk,
    Spiky = Hell,
    Chaos = DarkAtmosphere
};

enum class Role : uint8_t {
    Main,
    Accent,
    Glow
};

struct Piece {
    float x = 0.f;
    float y = 0.f;
    float w = 0.f;
    float h = 0.f;
    float rot = 0.f;

    Role role = Role::Main;
    int fade = 0;
};

struct Params {
    uint64_t seed = 1;
    Style style = Style::Monochrome;

    int detail = 5;
    float thickness = 2.5f;
    int fadeSteps = 4;

    bool frames = true;
    bool spires = true;
    bool dots = true;
    bool streaks = true;

    size_t maxPieces = 1500;
};

Style styleFromString(std::string const& s);

std::vector<Piece> generate(
    std::vector<Cell> const& cells,
    Params const& p
);

} // namespace autodecor
