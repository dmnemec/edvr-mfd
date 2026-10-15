#include "mfd_renderer.h"
#include "mfd_font.h"
#include "mfd_display_schema.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace edvr::mfd {

MfdRenderer::MfdRenderer(int width, int height)
    : m_width(width), m_height(height), m_pixels(width * height, palette::kBackground.toRgba()) {}

void MfdRenderer::resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    m_width = width;
    m_height = height;
    m_pixels.assign(width * height, palette::kBackground.toRgba());
}

void MfdRenderer::createOrUpdateD3D11Srv(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11ShaderResourceView** outSrv, bool forceUpdate) {
    if (!device || !context || !outSrv || m_width <= 0 || m_height <= 0 || m_pixels.empty()) return;

    if (!m_d3dTexture || m_texWidth != m_width || m_texHeight != m_height) {
        m_d3dTexture.Reset();
        m_d3dSrv.Reset();
        m_texWidth = m_width;
        m_texHeight = m_height;

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = m_width;
        desc.Height = m_height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initData{};
        initData.pSysMem = m_pixels.data();
        initData.SysMemPitch = m_width * sizeof(uint32_t);

        if (FAILED(device->CreateTexture2D(&desc, &initData, &m_d3dTexture))) return;
        if (FAILED(device->CreateShaderResourceView(m_d3dTexture.Get(), nullptr, &m_d3dSrv))) return;
    } else if (forceUpdate) {
        context->UpdateSubresource(m_d3dTexture.Get(), 0, nullptr, m_pixels.data(), m_width * sizeof(uint32_t), 0);
    }

    *outSrv = m_d3dSrv.Get();
}

void MfdRenderer::clear(MfdColor color) {
    std::fill(m_pixels.begin(), m_pixels.end(), color.toRgba());
}

void MfdRenderer::drawPixel(int x, int y, MfdColor color) {
    if (x < 0 || x >= m_width || y < 0 || y >= m_height) return;
    m_pixels[y * m_width + x] = color.toRgba();
}

void MfdRenderer::drawRect(int x, int y, int w, int h, MfdColor color) {
    if (w <= 0 || h <= 0) return;
    drawLine(x, y, x + w - 1, y, color);
    drawLine(x, y + h - 1, x + w - 1, y + h - 1, color);
    drawLine(x, y, x, y + h - 1, color);
    drawLine(x + w - 1, y, x + w - 1, y + h - 1, color);
}

void MfdRenderer::drawRectFilled(int x, int y, int w, int h, MfdColor color) {
    if (w <= 0 || h <= 0) return;
    int x0 = std::max(0, x);
    int y0 = std::max(0, y);
    int x1 = std::min(m_width, x + w);
    int y1 = std::min(m_height, y + h);

    uint32_t val = color.toRgba();
    for (int r = y0; r < y1; ++r) {
        uint32_t* row = &m_pixels[r * m_width + x0];
        std::fill(row, row + (x1 - x0), val);
    }
}

void MfdRenderer::drawRectDashed(int x, int y, int w, int h, MfdColor color, int dashLen) {
    if (w <= 0 || h <= 0) return;
    int step = (std::max)(2, dashLen * 2);
    for (int i = 0; i < w; i += step) {
        int len = (std::min)(dashLen, w - i);
        for (int k = 0; k < len; ++k) {
            drawPixel(x + i + k, y, color);
            drawPixel(x + i + k, y + h - 1, color);
        }
    }
    for (int i = 0; i < h; i += step) {
        int len = (std::min)(dashLen, h - i);
        for (int k = 0; k < len; ++k) {
            drawPixel(x, y + i + k, color);
            drawPixel(x + w - 1, y + i + k, color);
        }
    }
}

namespace {

constexpr uint16_t pack3x5(uint16_t r0, uint16_t r1, uint16_t r2, uint16_t r3, uint16_t r4) {
    return (r0 << 12) | (r1 << 9) | (r2 << 6) | (r3 << 3) | r4;
}

uint16_t getGlyph3x5(char c) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    switch (c) {
        case 'A': return pack3x5(0b010, 0b101, 0b111, 0b101, 0b101);
        case 'B': return pack3x5(0b110, 0b101, 0b110, 0b101, 0b110);
        case 'C': return pack3x5(0b011, 0b100, 0b100, 0b100, 0b011);
        case 'D': return pack3x5(0b110, 0b101, 0b101, 0b101, 0b110);
        case 'E': return pack3x5(0b111, 0b100, 0b110, 0b100, 0b111);
        case 'F': return pack3x5(0b111, 0b100, 0b110, 0b100, 0b100);
        case 'G': return pack3x5(0b011, 0b100, 0b101, 0b101, 0b011);
        case 'H': return pack3x5(0b101, 0b101, 0b111, 0b101, 0b101);
        case 'I': return pack3x5(0b111, 0b010, 0b010, 0b010, 0b111);
        case 'J': return pack3x5(0b001, 0b001, 0b001, 0b101, 0b010);
        case 'K': return pack3x5(0b101, 0b101, 0b110, 0b101, 0b101);
        case 'L': return pack3x5(0b100, 0b100, 0b100, 0b100, 0b111);
        case 'M': return pack3x5(0b101, 0b111, 0b101, 0b101, 0b101);
        case 'N': return pack3x5(0b110, 0b101, 0b101, 0b101, 0b101);
        case 'O': return pack3x5(0b010, 0b101, 0b101, 0b101, 0b010);
        case 'P': return pack3x5(0b110, 0b101, 0b110, 0b100, 0b100);
        case 'Q': return pack3x5(0b010, 0b101, 0b101, 0b110, 0b001);
        case 'R': return pack3x5(0b110, 0b101, 0b110, 0b101, 0b101);
        case 'S': return pack3x5(0b011, 0b100, 0b010, 0b001, 0b110);
        case 'T': return pack3x5(0b111, 0b010, 0b010, 0b010, 0b010);
        case 'U': return pack3x5(0b101, 0b101, 0b101, 0b101, 0b011);
        case 'V': return pack3x5(0b101, 0b101, 0b101, 0b101, 0b010);
        case 'W': return pack3x5(0b101, 0b101, 0b101, 0b111, 0b101);
        case 'X': return pack3x5(0b101, 0b101, 0b010, 0b101, 0b101);
        case 'Y': return pack3x5(0b101, 0b101, 0b010, 0b010, 0b010);
        case 'Z': return pack3x5(0b111, 0b001, 0b010, 0b100, 0b111);
        case '0': return pack3x5(0b111, 0b101, 0b101, 0b101, 0b111);
        case '1': return pack3x5(0b010, 0b110, 0b010, 0b010, 0b111);
        case '2': return pack3x5(0b111, 0b001, 0b111, 0b100, 0b111);
        case '3': return pack3x5(0b111, 0b001, 0b111, 0b001, 0b111);
        case '4': return pack3x5(0b101, 0b101, 0b111, 0b001, 0b001);
        case '5': return pack3x5(0b111, 0b100, 0b111, 0b001, 0b111);
        case '6': return pack3x5(0b111, 0b100, 0b111, 0b101, 0b111);
        case '7': return pack3x5(0b111, 0b001, 0b010, 0b010, 0b010);
        case '8': return pack3x5(0b111, 0b101, 0b111, 0b101, 0b111);
        case '9': return pack3x5(0b111, 0b101, 0b111, 0b001, 0b111);
        case '-': return pack3x5(0b000, 0b000, 0b111, 0b000, 0b000);
        case '+': return pack3x5(0b000, 0b010, 0b111, 0b010, 0b000);
        case '!': return pack3x5(0b010, 0b010, 0b010, 0b000, 0b010);
        case '?': return pack3x5(0b110, 0b001, 0b010, 0b000, 0b010);
        default:  return 0;
    }
}

} // anonymous namespace

void MfdRenderer::drawChar3x5(int x, int y, char c, MfdColor color) {
    uint16_t mask = getGlyph3x5(c);
    if (!mask) return;
    for (int r = 0; r < 5; ++r) {
        uint16_t rowBits = (mask >> ((4 - r) * 3)) & 7;
        for (int col = 0; col < 3; ++col) {
            if ((rowBits >> (2 - col)) & 1) {
                drawPixel(x + col, y + r, color);
            }
        }
    }
}

void MfdRenderer::drawTextMini(int bx, int by, int bw, int bh, const std::string& text, MfdColor color) {
    if (text.empty()) return;
    if (text.size() == 1) {
        int tx = bx + (bw - 3) / 2;
        int ty = by + (bh - 5) / 2;
        drawChar3x5(tx, ty, text[0], color);
    } else {
        int tx = bx + (bw - 7) / 2;
        int ty = by + (bh - 5) / 2;
        drawChar3x5(tx, ty, text[0], color);
        drawChar3x5(tx + 4, ty, text[1], color);
    }
}

void MfdRenderer::drawIcon(int x, int y, MfdIcon icon, MfdColor color, int size) {
    if (size <= 0 || icon == MfdIcon::kNone) return;

    switch (icon) {
        case MfdIcon::kFuel: {
            // Fuel Jerry Can: top handle, body, cross embossing
            drawLine(x + 2, y, x + size - 3, y, color);
            drawLine(x + 2, y, x + 2, y + 2, color);
            drawLine(x + size - 3, y, x + size - 3, y + 2, color);
            drawRect(x + 1, y + 2, size - 2, size - 3, color);
            if (size >= 8) {
                drawLine(x + 3, y + 4, x + size - 4, y + size - 3, color);
                drawLine(x + 3, y + size - 3, x + size - 4, y + 4, color);
            }
            break;
        }
        case MfdIcon::kWarning: {
            // Hazard triangle with exclamation mark
            int midX = x + size / 2;
            drawLine(midX, y, x, y + size - 1, color);
            drawLine(midX, y, x + size - 1, y + size - 1, color);
            drawLine(x, y + size - 1, x + size - 1, y + size - 1, color);
            if (size >= 8) {
                drawLine(midX, y + 3, midX, y + size - 5, color);
                drawPixel(midX, y + size - 3, color);
            }
            break;
        }
        case MfdIcon::kNeutron: {
            // Neutron Star / Pulsar with bipolar relativistic jets
            int midX = x + size / 2;
            int midY = y + size / 2;
            drawRectFilled(midX - 1, midY - 1, 3, 3, color);
            drawLine(midX, midY - 2, midX, y, color);
            drawLine(midX - 2, y + 2, midX, y, color);
            drawLine(midX + 2, y + 2, midX, y, color);

            drawLine(midX, midY + 2, midX, y + size - 1, color);
            drawLine(midX - 2, y + size - 3, midX, y + size - 1, color);
            drawLine(midX + 2, y + size - 3, midX, y + size - 1, color);

            drawPixel(x + 1, midY, color);
            drawPixel(x + size - 2, midY, color);
            drawPixel(x + 2, midY - 1, color);
            drawPixel(x + size - 3, midY - 1, color);
            drawPixel(x + 2, midY + 1, color);
            drawPixel(x + size - 3, midY + 1, color);
            break;
        }
        case MfdIcon::kWhitedwarf: {
            // White Dwarf: dense core with 4-point sparkle diffraction spikes
            int midX = x + size / 2;
            int midY = y + size / 2;
            drawRectFilled(midX - 1, midY - 1, 3, 3, color);
            drawLine(x, midY, x + size - 1, midY, color);
            drawLine(midX, y, midX, y + size - 1, color);
            drawPixel(midX - 2, midY - 2, color);
            drawPixel(midX + 2, midY - 2, color);
            drawPixel(midX - 2, midY + 2, color);
            drawPixel(midX + 2, midY + 2, color);
            break;
        }
        case MfdIcon::kBio: {
            // Exobiology Sprout / Organic Leaf
            int midX = x + size / 2;
            drawLine(midX, y + size / 2, midX, y + 2, color);
            drawLine(midX, y + 2, x + size - 2, y + 1, color);
            drawLine(midX, y + size / 2, x + size - 4, y + size / 2, color);
            drawLine(x + size - 4, y + size / 2, x + size - 2, y + 1, color);
            drawLine(x + 2, y + size - 1, midX, y + size / 2, color);
            drawLine(x + 3, y + size - 3, x + 1, y + size - 5, color);
            break;
        }
        case MfdIcon::kGeo: {
            // Geological Twin Mountain Peaks / Volcanic Ridge
            drawLine(x, y + size - 1, x + 4, y + 2, color);
            drawLine(x + 4, y + 2, x + 7, y + 7, color);
            drawLine(x + 7, y + 7, x + 10, y + 4, color);
            drawLine(x + 10, y + 4, x + size - 1, y + size - 1, color);
            drawLine(x, y + size - 1, x + size - 1, y + size - 1, color);
            drawPixel(x + 4, y, color);
            break;
        }
        case MfdIcon::kCheck: {
            // Solid Checkmark
            int midX = x + size / 3;
            int midY = y + size - 3;
            drawLine(x + 1, y + size / 2, midX, midY, color);
            drawLine(x + 1, y + size / 2 + 1, midX, midY + 1, color);
            drawLine(midX, midY, x + size - 2, y + 1, color);
            drawLine(midX, midY + 1, x + size - 2, y + 2, color);
            break;
        }
        case MfdIcon::kLock: {
            // Padlock
            int midX = x + size / 2;
            drawLine(x + 4, y + 1, x + size - 5, y + 1, color);
            drawLine(x + 3, y + 2, x + 3, y + 5, color);
            drawLine(x + size - 4, y + 2, x + size - 4, y + 5, color);
            drawRect(x + 2, y + 5, size - 4, size - 5, color);
            drawPixel(midX, y + 7, color);
            drawLine(midX, y + 8, midX, y + 9, color);
            break;
        }
        case MfdIcon::kStar: {
            // 5-point Star
            int midX = x + size / 2;
            int rUpperX = x + size - 1;
            int rUpperY = y + size * 4 / 10;
            int rLowerX = x + size * 8 / 10;
            int rLowerY = y + size - 1;
            int lLowerX = x + size * 2 / 10;
            int lLowerY = y + size - 1;
            int lUpperX = x;
            int lUpperY = y + size * 4 / 10;

            drawLine(midX, y, rLowerX, rLowerY, color);
            drawLine(rLowerX, rLowerY, lUpperX, lUpperY, color);
            drawLine(lUpperX, lUpperY, rUpperX, rUpperY, color);
            drawLine(rUpperX, rUpperY, lLowerX, lLowerY, color);
            drawLine(lLowerX, lLowerY, midX, y, color);
            break;
        }
        case MfdIcon::kNone:
        default:
            break;
    }
}

void MfdRenderer::drawProgressBar(int x, int y, int width, int height, float value, float maxVal, int segments, MfdColor fillColor, MfdColor troughColor, MfdColor borderColor) {
    if (width <= 0 || height <= 0) return;
    float safeMax = (maxVal > 0.0001f) ? maxVal : 1.0f;
    float ratio = std::clamp(value / safeMax, 0.0f, 1.0f);

    drawRectFilled(x, y, width, height, troughColor);
    drawRect(x, y, width, height, borderColor);

    int innerX = x + 2;
    int innerY = y + 2;
    int innerW = width - 4;
    int innerH = height - 4;
    if (innerW <= 0 || innerH <= 0) return;

    if (segments <= 1) {
        int fillW = static_cast<int>(std::round(ratio * innerW));
        if (fillW > 0) {
            drawRectFilled(innerX, innerY, fillW, innerH, fillColor);
        }
    } else {
        int gap = (innerW >= segments * 4) ? 2 : 1;
        int totalGaps = (segments - 1) * gap;
        int availW = innerW - totalGaps;
        if (availW < segments) {
            int fillW = static_cast<int>(std::round(ratio * innerW));
            if (fillW > 0) {
                drawRectFilled(innerX, innerY, fillW, innerH, fillColor);
            }
            return;
        }

        int activeSegs = static_cast<int>(std::round(ratio * segments));
        float segWidthExact = static_cast<float>(availW) / segments;

        for (int s = 0; s < segments; ++s) {
            int sx0 = innerX + static_cast<int>(s * (segWidthExact + gap));
            int sx1 = innerX + static_cast<int>((s + 1) * segWidthExact + s * gap);
            int segW = (std::max)(1, sx1 - sx0);

            if (s < activeSegs) {
                drawRectFilled(sx0, innerY, segW, innerH, fillColor);
            } else {
                drawRectFilled(sx0, innerY, segW, innerH, MfdColor(troughColor.r, troughColor.g, troughColor.b, 80));
            }
        }
    }
}

void MfdRenderer::drawCheck(int x, int y, const MfdCheckItem& check, MfdColor col, MfdColor dimCol, MfdColor bgCol, int size) {
    if (size <= 0) return;
    int bw = size;
    int bh = size;

    switch (check.state) {
        case MfdCheckState::kDone: {
            drawRectFilled(x, y, bw, bh, col);
            if (!check.label.empty()) {
                drawTextMini(x, y, bw, bh, check.label, bgCol);
            } else {
                MfdColor checkCol(bgCol.r, bgCol.g, bgCol.b, 255);
                drawIcon(x, y, MfdIcon::kCheck, checkCol, bw);
            }
            break;
        }
        case MfdCheckState::kTodo: {
            drawRect(x, y, bw, bh, col);
            if (!check.label.empty()) {
                drawTextMini(x, y, bw, bh, check.label, col);
            }
            break;
        }
        case MfdCheckState::kNa: {
            drawRectDashed(x, y, bw, bh, dimCol, 2);
            if (!check.label.empty()) {
                drawTextMini(x, y, bw, bh, check.label, dimCol);
            }
            break;
        }
    }
}

void MfdRenderer::drawLine(int x0, int y0, int x1, int y1, MfdColor color) {
    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    while (true) {
        drawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void MfdRenderer::drawScanlines(MfdColor color, int step) {
    if (step <= 0) return;
    uint32_t c = color.toRgba();
    for (int y = 0; y < m_height; y += step) {
        uint32_t* row = &m_pixels[y * m_width];
        for (int x = 0; x < m_width; ++x) {
            row[x] = c;
        }
    }
}

void MfdRenderer::drawChar(int x, int y, char c, MfdColor color, int scale) {
    const uint8_t* glyph = MfdFont::getGlyph(c);
    for (int r = 0; r < MfdFont::kCharHeight; ++r) {
        uint8_t rowBits = glyph[r];
        for (int col = 0; col < MfdFont::kCharWidth; ++col) {
            if ((rowBits >> (7 - col)) & 1) {
                if (scale == 1) {
                    drawPixel(x + col, y + r, color);
                } else {
                    drawRectFilled(x + col * scale, y + r * scale, scale, scale, color);
                }
            }
        }
    }
}

void MfdRenderer::drawText(int x, int y, const std::string& text, MfdColor color, int scale) {
    int curX = x;
    int charW = MfdFont::kCharWidth * scale;
    for (char c : text) {
        if (c == '\n') {
            curX = x;
            y += MfdFont::kCharHeight * scale;
            continue;
        }
        drawChar(curX, y, c, color, scale);
        curX += charW;
    }
}

void MfdRenderer::drawTextRight(int rightX, int y, const std::string& text, MfdColor color, int scale) {
    int textW = static_cast<int>(text.size()) * MfdFont::kCharWidth * scale;
    drawText(rightX - textW, y, text, color, scale);
}

void MfdRenderer::render(const MfdViewModel& model, float backgroundOpacity, MfdColorTheme theme, bool useCustomColor, MfdColor customColor) {
    auto paletteSet = palette::getTheme(theme);
    if (useCustomColor) {
        paletteSet.mainColor = customColor;
        float brightFactor = 1.3f;
        float dimFactor = 0.55f;
        uint8_t brR = static_cast<uint8_t>((std::min)(255.0f, customColor.r * brightFactor));
        uint8_t brG = static_cast<uint8_t>((std::min)(255.0f, customColor.g * brightFactor));
        uint8_t brB = static_cast<uint8_t>((std::min)(255.0f, customColor.b * brightFactor));
        paletteSet.brightColor = MfdColor(brR, brG, brB, customColor.a);

        uint8_t dimR = static_cast<uint8_t>(customColor.r * dimFactor);
        uint8_t dimG = static_cast<uint8_t>(customColor.g * dimFactor);
        uint8_t dimB = static_cast<uint8_t>(customColor.b * dimFactor);
        paletteSet.dimColor = MfdColor(dimR, dimG, dimB, customColor.a);
        paletteSet.borderColor = customColor;
    }

    m_activePalette = paletteSet;

    // 1. Translucent dark cockpit glass background
    uint8_t bgAlpha = static_cast<uint8_t>(std::clamp(backgroundOpacity, 0.0f, 1.0f) * 255.0f);
    MfdColor bgColor(paletteSet.bgColor.r, paletteSet.bgColor.g, paletteSet.bgColor.b, bgAlpha);
    clear(bgColor);

    if (bgAlpha > 20) {
        drawScanlines(MfdColor(paletteSet.bgColor.r / 2, paletteSet.bgColor.g / 2, paletteSet.bgColor.b / 2, static_cast<uint8_t>(bgAlpha * 0.75f)), 3);
    }

    // Color theme based on focus state:
    MfdColor mainColor = paletteSet.mainColor;
    MfdColor dimColor = model.isFocused ? paletteSet.mainColor : paletteSet.dimColor;
    MfdColor borderColor = model.isFocused ? paletteSet.brightColor : paletteSet.borderColor;

    // 2. Outer cockpit bezel frame and corner brackets
    int margin = 4;
    int w = m_width - margin * 2;
    int h = m_height - margin * 2;
    int bracketLen = 18;

    // Corner brackets
    drawLine(margin, margin, margin + bracketLen, margin, borderColor);
    drawLine(margin, margin, margin, margin + bracketLen, borderColor);
    drawLine(margin + w - bracketLen, margin, margin + w, margin, borderColor);
    drawLine(margin + w, margin, margin + w, margin + bracketLen, borderColor);
    drawLine(margin, margin + h - bracketLen, margin, margin + h, borderColor);
    drawLine(margin, margin + h, margin + bracketLen, margin + h, borderColor);
    drawLine(margin + w - bracketLen, margin + h, margin + w, margin + h, borderColor);
    drawLine(margin + w, margin + h - bracketLen, margin + w, margin + h, borderColor);

    // Subtle frame perimeter
    MfdColor faintBorder(borderColor.r, borderColor.g, borderColor.b, 80);
    drawRect(margin, margin, w, h, faintBorder);

    // 3. Render Header
    renderHeader(model, mainColor, dimColor);

    // 4. Render Tabs
    renderTabs(model, mainColor, dimColor);

    // 5. Render Tab Content
    const auto* tab = model.currentTab();
    if (tab) {
        switch (tab->type) {
            case MfdTabType::kList:
                renderListTab(*tab, model.isFocused, mainColor, dimColor);
                break;
            case MfdTabType::kKeyValue:
                renderKeyValueTab(*tab, mainColor, dimColor);
                break;
            case MfdTabType::kText:
                renderTextTab(*tab, mainColor, dimColor);
                break;
            case MfdTabType::kBars:
                renderBarsTab(*tab, model.isFocused, mainColor, dimColor);
                break;
        }
    }

    // 6. Render Footer
    renderFooter(model, dimColor);
}

void MfdRenderer::renderHeader(const MfdViewModel& model, MfdColor mainColor, MfdColor dimColor) {
    int x = 16;
    int y = 14;

    // Header Title
    drawText(x, y, model.title, mainColor, 1);

    // Subtitle
    if (!model.subtitle.empty()) {
        drawText(x + static_cast<int>(model.title.size() + 2) * 8, y, "// " + model.subtitle, dimColor, 1);
    }

    // Status Badge
    if (!model.statusBadge.empty()) {
        std::string badgeStr = "[" + model.statusBadge + "]";
        MfdNamedColor nCol = model.statusBadgeNamedColor;
        if (nCol == MfdNamedColor::kNormal && !model.statusBadgeColorName.empty() && model.statusBadgeColorName != "normal") {
            nCol = parseNamedColor(model.statusBadgeColorName);
        }
        MfdColor badgeCol = resolveNamedColor(nCol, m_activePalette);

        int badgeW = static_cast<int>(badgeStr.size()) * 8;
        int badgeX = m_width - 24 - badgeW;

        if (nCol == MfdNamedColor::kCrit) {
            drawRectFilled(badgeX - 4, y - 2, badgeW + 8, 16, MfdColor(255, 30, 30, 60));
            drawRect(badgeX - 4, y - 2, badgeW + 8, 16, palette::kAlertRed);
        } else if (nCol == MfdNamedColor::kWarn) {
            drawRectFilled(badgeX - 4, y - 2, badgeW + 8, 16, MfdColor(255, 150, 0, 40));
            drawRect(badgeX - 4, y - 2, badgeW + 8, 16, palette::kAmberBright);
        } else if (nCol == MfdNamedColor::kOk) {
            drawRectFilled(badgeX - 4, y - 2, badgeW + 8, 16, MfdColor(0, 240, 60, 30));
        }

        drawTextRight(m_width - 24, y, badgeStr, badgeCol, 1);
    }

    // Header divider line
    drawLine(12, 34, m_width - 12, 34, m_activePalette.borderColor);
}

void MfdRenderer::renderTabs(const MfdViewModel& model, MfdColor mainColor, MfdColor dimColor) {
    if (model.tabs.empty()) return;

    int curX = 16;
    int y = 42;

    for (size_t i = 0; i < model.tabs.size(); ++i) {
        bool isActive = (static_cast<int>(i) == model.activeTabIndex);
        const auto& title = model.tabs[i].title;

        if (isActive) {
            std::string label = "[ " + title + " ]";
            int tabW = static_cast<int>(label.size()) * 8;
            drawRectFilled(curX - 2, y - 2, tabW + 4, 18, MfdColor(m_activePalette.mainColor.r, m_activePalette.mainColor.g, m_activePalette.mainColor.b, 50));
            drawText(curX, y, label, mainColor, 1);
            curX += tabW + 12;
        } else {
            drawText(curX, y, title, dimColor, 1);
            curX += static_cast<int>(title.size()) * 8 + 16;
        }
    }

    // Tab separator line
    drawLine(12, 62, m_width - 12, 62, m_activePalette.borderColor);
}

void MfdRenderer::renderListTab(const MfdTab& tab, bool focused, MfdColor mainColor, MfdColor dimColor) {
    int startY = 72;
    int rowH = 26;
    int maxRows = (m_height - startY - 34) / rowH;
    if (maxRows <= 0) maxRows = 1;

    int totalItems = static_cast<int>(tab.items.size());
    int maxScroll = (std::max)(0, totalItems - maxRows);
    int scroll = (std::clamp)(tab.scrollOffset, 0, maxScroll);

    const auto& theme = m_activePalette;

    for (int i = 0; i < maxRows && (scroll + i) < totalItems; ++i) {
        int itemIdx = scroll + i;
        const auto& item = tab.items[itemIdx];
        int y = startY + i * rowH;
        bool isSelected = (itemIdx == tab.selectedIndex);

        MfdColor invColor(16, 12, 8, 255);

        if (isSelected) {
            if (focused) {
                // Vibrant filled highlight for focused selection
                drawRectFilled(14, y - 2, m_width - 28, rowH - 2, theme.brightColor);
            } else {
                // Unfocused selection: subtle hollow outline
                drawRect(14, y - 2, m_width - 28, rowH - 2, dimColor);
            }
        }

        int curX = 20;

        // 1. Checks array (0-6 small square boxes)
        if (!item.checks.empty()) {
            for (const auto& chk : item.checks) {
                MfdColor col;
                MfdColor dimCol;
                MfdColor bgCol;
                if (isSelected && focused) {
                    col = invColor;
                    dimCol = MfdColor(80, 50, 20, 255);
                    bgCol = theme.brightColor;
                } else {
                    col = mainColor;
                    dimCol = theme.dimColor;
                    bgCol = theme.bgColor;
                }
                drawCheck(curX, y + 4, chk, col, dimCol, bgCol, 10);
                curX += 13;
            }
            curX += 2;
        }

        // 2. Icon glyph
        if (item.icon != MfdIcon::kNone) {
            MfdColor iconCol;
            if (isSelected && focused) {
                iconCol = invColor;
            } else {
                switch (item.icon) {
                    case MfdIcon::kWarning:    iconCol = resolveNamedColor(MfdNamedColor::kWarn, theme); break;
                    case MfdIcon::kBio:        iconCol = resolveNamedColor(MfdNamedColor::kOk, theme); break;
                    case MfdIcon::kCheck:      iconCol = resolveNamedColor(MfdNamedColor::kOk, theme); break;
                    case MfdIcon::kNeutron:    iconCol = theme.accentColor; break;
                    case MfdIcon::kWhitedwarf: iconCol = MfdColor(240, 245, 255); break;
                    case MfdIcon::kFuel:       iconCol = theme.accentColor; break;
                    case MfdIcon::kLock:       iconCol = resolveNamedColor(MfdNamedColor::kWarn, theme); break;
                    case MfdIcon::kGeo:        iconCol = theme.mainColor; break;
                    case MfdIcon::kStar:       iconCol = theme.mainColor; break;
                    default:                   iconCol = mainColor; break;
                }
            }
            drawIcon(curX, y + 3, item.icon, iconCol, 12);
            curX += 16;
        }

        // 3. Label text
        MfdColor lblCol = (isSelected && focused) ? invColor : mainColor;
        drawText(curX, y + 1, item.label, lblCol, 1);

        // 4. Sublabel text (aligned with label)
        if (!item.sublabel.empty() && rowH >= 24) {
            MfdColor subColor = (isSelected && focused) ? MfdColor(40, 25, 10, 255) : dimColor;
            drawText(curX, y + 12, item.sublabel, subColor, 1);
        }

        // 5. Value and Badge on right
        int rightX = m_width - 24;
        if (!item.value.empty()) {
            MfdColor valCol;
            if (isSelected && focused) {
                valCol = invColor;
            } else {
                MfdNamedColor nCol = item.valueNamedColor;
                if (nCol == MfdNamedColor::kNormal && !item.valueColorName.empty() && item.valueColorName != "normal") {
                    nCol = parseNamedColor(item.valueColorName);
                }
                if (nCol != MfdNamedColor::kNormal) {
                    valCol = resolveNamedColor(nCol, theme);
                } else if (item.valueColor != palette::kAmberNormal) {
                    valCol = item.valueColor;
                } else {
                    valCol = isSelected ? mainColor : dimColor;
                }
            }
            drawTextRight(rightX, y + 1, item.value, valCol, 1);
            rightX -= static_cast<int>(item.value.size()) * 8 + 8;
        }

        if (!item.badge.empty()) {
            MfdColor badgeCol;
            if (isSelected && focused) {
                badgeCol = invColor;
            } else if (item.badgeColor == palette::kCyanAccent) {
                badgeCol = theme.accentColor;
            } else {
                badgeCol = item.badgeColor;
            }
            std::string bText = "[" + item.badge + "]";
            drawTextRight(rightX, y + 1, bText, badgeCol, 1);
        }
    }
}

void MfdRenderer::renderKeyValueTab(const MfdTab& tab, MfdColor mainColor, MfdColor dimColor) {
    (void)mainColor;
    int startY = 74;
    int rowH = 22;
    int maxRows = (m_height - startY - 34) / rowH;
    if (maxRows <= 0) maxRows = 1;

    int totalItems = static_cast<int>(tab.keyValues.size());
    int maxScroll = (std::max)(0, totalItems - maxRows);
    int scroll = (std::clamp)(tab.scrollOffset, 0, maxScroll);

    const auto& theme = m_activePalette;

    for (int i = 0; i < maxRows && (scroll + i) < totalItems; ++i) {
        int itemIdx = scroll + i;
        const auto& kv = tab.keyValues[itemIdx];
        int y = startY + i * rowH;
        bool isSelected = (itemIdx == tab.selectedIndex);

        if (isSelected) {
            drawRectFilled(14, y - 2, m_width - 28, rowH, MfdColor(theme.mainColor.r / 5, theme.mainColor.g / 5, theme.mainColor.b / 5, 140));
            drawLine(14, y - 2, 14, y + rowH - 3, theme.mainColor);
            drawText(20, y, kv.key, theme.mainColor, 1);
        } else {
            drawText(20, y, kv.key, dimColor, 1);
        }

        MfdNamedColor nCol = kv.valueNamedColor;
        if (nCol == MfdNamedColor::kNormal && !kv.valueColorName.empty() && kv.valueColorName != "normal") {
            nCol = parseNamedColor(kv.valueColorName);
        }
        MfdColor valCol;
        if (isSelected) {
            valCol = theme.mainColor;
        } else if (nCol != MfdNamedColor::kNormal) {
            valCol = resolveNamedColor(nCol, theme);
        } else {
            valCol = kv.valueColor;
        }

        // Dotted leader line between key and value
        int dotStart = 24 + static_cast<int>(kv.key.size()) * 8;
        int dotEnd = m_width - 32 - static_cast<int>(kv.value.size()) * 8;
        for (int dx = dotStart; dx < dotEnd; dx += 12) {
            drawPixel(dx, y + 8, isSelected ? theme.mainColor : theme.dimColor);
        }

        // Value on right
        drawTextRight(m_width - 24, y, kv.value, valCol, 1);
    }
}

void MfdRenderer::renderTextTab(const MfdTab& tab, MfdColor mainColor, MfdColor dimColor) {
    (void)dimColor;
    int startY = 72;
    int lineH = 18;
    int maxLines = (m_height - startY - 34) / lineH;

    int totalLines = static_cast<int>(tab.lines.size());
    int maxScroll = (std::max)(0, totalLines - maxLines);
    int scroll = (std::clamp)(tab.textScrollLine, 0, maxScroll);

    for (int i = 0; i < maxLines && (scroll + i) < totalLines; ++i) {
        int y = startY + i * lineH;
        drawText(20, y, tab.lines[scroll + i], mainColor, 1);
    }
}

void MfdRenderer::renderBarsTab(const MfdTab& tab, bool focused, MfdColor mainColor, MfdColor dimColor) {
    int startY = 72;
    int rowH = 30;
    int maxRows = (m_height - startY - 34) / rowH;
    if (maxRows <= 0) maxRows = 1;

    int totalItems = static_cast<int>(tab.barItems.size());
    int maxScroll = (std::max)(0, totalItems - maxRows);
    int scroll = (std::clamp)(tab.scrollOffset, 0, maxScroll);

    const auto& theme = m_activePalette;

    for (int i = 0; i < maxRows && (scroll + i) < totalItems; ++i) {
        int itemIdx = scroll + i;
        const auto& bar = tab.barItems[itemIdx];
        int y = startY + i * rowH;
        bool isSelected = (itemIdx == tab.selectedIndex);

        if (isSelected) {
            if (focused) {
                drawRectFilled(14, y - 2, m_width - 28, rowH - 2, MfdColor(theme.mainColor.r / 4, theme.mainColor.g / 4, theme.mainColor.b / 4, 120));
                drawLine(14, y - 2, 14, y + rowH - 4, theme.brightColor);
                drawLine(15, y - 2, 15, y + rowH - 4, theme.brightColor);
            } else {
                drawRect(14, y - 2, m_width - 28, rowH - 2, dimColor);
            }
        }

        // 1. Label on left
        MfdColor lblCol = isSelected ? (focused ? theme.brightColor : mainColor) : mainColor;
        drawText(20, y + 2, bar.label, lblCol, 1);

        // 2. Numeric readout on right
        std::string readout;
        if (std::abs(bar.value - std::round(bar.value)) < 0.05f && std::abs(bar.max - std::round(bar.max)) < 0.05f) {
            readout = std::to_string(static_cast<int>(std::round(bar.value))) + " / " + std::to_string(static_cast<int>(std::round(bar.max)));
        } else {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.1f / %.1f", bar.value, bar.max);
            readout = buf;
        }

        MfdColor readoutCol = isSelected ? theme.brightColor : dimColor;
        drawTextRight(m_width - 24, y + 2, readout, readoutCol, 1);

        // 3. Progress bar in middle
        int labelW = static_cast<int>(bar.label.size()) * 8;
        int readoutW = static_cast<int>(readout.size()) * 8;
        int barX = 20 + labelW + 10;
        if (m_width >= 400 && barX < 110) barX = 110;
        int barRight = m_width - 28 - readoutW - 10;
        int barW = barRight - barX;
        int barH = 14;

        if (barW >= 20) {
            MfdNamedColor nCol = bar.namedColor;
            if (nCol == MfdNamedColor::kNormal && !bar.colorName.empty() && bar.colorName != "normal") {
                nCol = parseNamedColor(bar.colorName);
            }
            MfdColor barColor = resolveNamedColor(nCol, theme);
            MfdColor troughColor(theme.bgColor.r + 20, theme.bgColor.g + 20, theme.bgColor.b + 20, 200);
            MfdColor borderColor(theme.borderColor.r, theme.borderColor.g, theme.borderColor.b, 100);
            drawProgressBar(barX, y + 2, barW, barH, bar.value, bar.max, bar.segments, barColor, troughColor, borderColor);
        }
    }
}

void MfdRenderer::renderFooter(const MfdViewModel& model, MfdColor dimColor) {
    int y = m_height - 22;
    drawLine(12, y - 4, m_width - 12, y - 4, m_activePalette.borderColor);

    if (!model.footerHint.empty()) {
        drawText(16, y, model.footerHint, dimColor, 1);
    }

    if (model.isFocused) {
        drawTextRight(m_width - 20, y, "[FOCUSED]", m_activePalette.accentColor, 1);
    } else {
        drawTextRight(m_width - 20, y, "[LOOK TO ACTIVATE]", dimColor, 1);
    }
}

} // namespace edvr::mfd
