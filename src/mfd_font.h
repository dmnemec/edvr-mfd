#pragma once

#include <cstdint>

namespace edvr::mfd {

// Monospace 8x16 font glyph data for standard ASCII (32 ' ' to 126 '~').
// Each character is 16 bytes, where each byte represents one row of 8 horizontal pixels (MSB is left).
struct MfdFont {
    static constexpr int kCharWidth = 8;
    static constexpr int kCharHeight = 16;
    static constexpr int kFirstChar = 32;
    static constexpr int kLastChar = 126;

    // Standard 8x16 console glyph rasterizer.
    // Retrieves the 16-row bitmask for a given character.
    static const uint8_t* getGlyph(char c);
};

} // namespace edvr::mfd
