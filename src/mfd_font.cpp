#include "mfd_font.h"

namespace edvr::mfd {

namespace {

// Standard classic 8x16 IBM/VGA font table for printable ASCII 32..126.
// Generated standard bitmaps for clean high-contrast cockpit legibility.
#include "mfd_font_data.inl"

} // namespace

const uint8_t* MfdFont::getGlyph(char c) {
    auto uc = static_cast<unsigned char>(c);
    if (uc < kFirstChar || uc > kLastChar) {
        uc = '?';
    }
    return &kFontData[(uc - kFirstChar) * kCharHeight];
}

} // namespace edvr::mfd
