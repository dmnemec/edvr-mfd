#include "mfd_renderer.h"
#include "mfd_font.h"
#include <algorithm>
#include <cmath>

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

void MfdRenderer::createOrUpdateD3D11Srv(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11ShaderResourceView** outSrv) {
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
    } else {
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
        // Compute bright, dim, and border colors based on customColor and its alpha/brightness
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

    // 1. Translucent dark cockpit glass background (0% backgroundOpacity = 100% transparent glass)
    uint8_t bgAlpha = static_cast<uint8_t>(std::clamp(backgroundOpacity, 0.0f, 1.0f) * 255.0f);
    MfdColor bgColor(paletteSet.bgColor.r, paletteSet.bgColor.g, paletteSet.bgColor.b, bgAlpha);
    clear(bgColor);

    if (bgAlpha > 20) {
        // Subtle cathode scanlines
        drawScanlines(MfdColor(paletteSet.bgColor.r / 2, paletteSet.bgColor.g / 2, paletteSet.bgColor.b / 2, static_cast<uint8_t>(bgAlpha * 0.75f)), 3);
    }

    // Color theme based on focus state:
    MfdColor mainColor = paletteSet.mainColor;
    MfdColor dimColor = model.isFocused ? paletteSet.mainColor : paletteSet.dimColor;
    MfdColor borderColor = model.isFocused ? paletteSet.brightColor : paletteSet.borderColor;

    // 2. Outer cockpit bezel frame and corner brackets:
    int margin = 4;
    int w = m_width - margin * 2;
    int h = m_height - margin * 2;
    int bracketLen = 18;

    // Corner brackets (Elite HUD styling)
    // Top-left
    drawLine(margin, margin, margin + bracketLen, margin, borderColor);
    drawLine(margin, margin, margin, margin + bracketLen, borderColor);
    // Top-right
    drawLine(margin + w - bracketLen, margin, margin + w, margin, borderColor);
    drawLine(margin + w, margin, margin + w, margin + bracketLen, borderColor);
    // Bottom-left
    drawLine(margin, margin + h - bracketLen, margin, margin + h, borderColor);
    drawLine(margin, margin + h, margin + bracketLen, margin + h, borderColor);
    // Bottom-right
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

    // Status Badge (e.g. "[ONLINE]")
    if (!model.statusBadge.empty()) {
        std::string badgeStr = "[" + model.statusBadge + "]";
        drawTextRight(m_width - 24, y, badgeStr, model.statusBadgeColor, 1);
    }

    // Header divider line
    drawLine(12, 34, m_width - 12, 34, palette::kFrameBorder);
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
            drawRectFilled(curX - 2, y - 2, tabW + 4, 18, MfdColor(255, 113, 0, 50));
            drawText(curX, y, label, mainColor, 1);
            curX += tabW + 12;
        } else {
            drawText(curX, y, title, dimColor, 1);
            curX += static_cast<int>(title.size()) * 8 + 16;
        }
    }

    // Tab separator line
    drawLine(12, 62, m_width - 12, 62, palette::kFrameBorder);
}

void MfdRenderer::renderListTab(const MfdTab& tab, bool focused, MfdColor mainColor, MfdColor dimColor) {
    int startY = 72;
    int rowH = 26;
    int maxRows = (m_height - startY - 34) / rowH;
    if (maxRows <= 0) maxRows = 1;

    int totalItems = static_cast<int>(tab.items.size());
    int maxScroll = (std::max)(0, totalItems - maxRows);
    int scroll = (std::clamp)(tab.scrollOffset, 0, maxScroll);

    for (int i = 0; i < maxRows && (scroll + i) < totalItems; ++i) {
        int itemIdx = scroll + i;
        const auto& item = tab.items[itemIdx];
        int y = startY + i * rowH;
        bool isSelected = (itemIdx == tab.selectedIndex);

        if (isSelected) {
            if (focused) {
                // Vibrant filled highlight for focused selection
                drawRectFilled(14, y - 2, m_width - 28, rowH - 2, palette::kAmberBright);
                // Inverted text color (dark on bright amber)
                MfdColor invColor(16, 12, 8, 255);
                drawText(20, y + 1, item.label, invColor, 1);
                if (!item.value.empty()) {
                    drawTextRight(m_width - 24, y + 1, item.value, invColor, 1);
                }
            } else {
                // Unfocused selection: subtle hollow outline
                drawRect(14, y - 2, m_width - 28, rowH - 2, dimColor);
                drawText(20, y + 1, item.label, mainColor, 1);
                if (!item.value.empty()) {
                    drawTextRight(m_width - 24, y + 1, item.value, mainColor, 1);
                }
            }
        } else {
            // Normal row
            drawText(20, y + 1, item.label, mainColor, 1);
            if (!item.value.empty()) {
                drawTextRight(m_width - 24, y + 1, item.value, dimColor, 1);
            }
        }

        // Sublabel (if room)
        if (!item.sublabel.empty() && rowH >= 24) {
            MfdColor subColor = (isSelected && focused) ? MfdColor(40, 25, 10, 255) : dimColor;
            drawText(28, y + 12, item.sublabel, subColor, 1);
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

    for (int i = 0; i < maxRows && (scroll + i) < totalItems; ++i) {
        int itemIdx = scroll + i;
        const auto& kv = tab.keyValues[itemIdx];
        int y = startY + i * rowH;
        bool isSelected = (itemIdx == tab.selectedIndex);

        if (isSelected) {
            drawRectFilled(14, y - 2, m_width - 28, rowH, MfdColor(mainColor.r / 5, mainColor.g / 5, mainColor.b / 5, 140));
            drawLine(14, y - 2, 14, y + rowH - 3, mainColor);
            drawText(20, y, kv.key, mainColor, 1);
        } else {
            // Key on left (dim/amber)
            drawText(20, y, kv.key, dimColor, 1);
        }

        // Dotted leader line between key and value
        int dotStart = 24 + static_cast<int>(kv.key.size()) * 8;
        int dotEnd = m_width - 32 - static_cast<int>(kv.value.size()) * 8;
        for (int dx = dotStart; dx < dotEnd; dx += 12) {
            drawPixel(dx, y + 8, isSelected ? mainColor : palette::kAmberDim);
        }

        // Value on right
        drawTextRight(m_width - 24, y, kv.value, isSelected ? mainColor : kv.valueColor, 1);
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

void MfdRenderer::renderFooter(const MfdViewModel& model, MfdColor dimColor) {
    int y = m_height - 22;
    drawLine(12, y - 4, m_width - 12, y - 4, palette::kFrameBorder);

    if (!model.footerHint.empty()) {
        drawText(16, y, model.footerHint, dimColor, 1);
    }

    if (model.isFocused) {
        drawTextRight(m_width - 20, y, "[FOCUSED]", palette::kCyanAccent, 1);
    } else {
        drawTextRight(m_width - 20, y, "[LOOK TO ACTIVATE]", dimColor, 1);
    }
}

} // namespace edvr::mfd
