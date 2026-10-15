#pragma once

#include "mfd_types.h"
#include "mfd_view_model.h"
#include <vector>
#include <cstdint>
#include <d3d11.h>
#include <wrl/client.h>

namespace edvr::mfd {

using Microsoft::WRL::ComPtr;

// Rasterizes MfdViewModel into an RGBA8 bitmap buffer matching
// Elite Dangerous's native cockpit HUD aesthetics.
class MfdRenderer {
public:
    MfdRenderer(int width = 512, int height = 384);

    int width() const { return m_width; }
    int height() const { return m_height; }
    const uint32_t* pixelData() const { return m_pixels.data(); }
    size_t pixelByteSize() const { return m_pixels.size() * sizeof(uint32_t); }

    void resize(int width, int height);

    // Set active color theme
    void setTheme(MfdColorTheme theme) { m_theme = theme; }
    MfdColorTheme theme() const { return m_theme; }

    // Render the complete view model onto the internal pixel buffer.
    void render(const MfdViewModel& model, float backgroundOpacity = 0.75f, MfdColorTheme theme = MfdColorTheme::kDefaultAmber, bool useCustomColor = false, MfdColor customColor = palette::kAmberNormal);

    // D3D11 Shader Resource View helper for rendering onto eye swapchains:
    void createOrUpdateD3D11Srv(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11ShaderResourceView** outSrv, bool forceUpdate = true);

    // Drawing primitives:
    void clear(MfdColor color);
    void drawPixel(int x, int y, MfdColor color);
    void drawRect(int x, int y, int w, int h, MfdColor color);
    void drawRectFilled(int x, int y, int w, int h, MfdColor color);
    void drawRectDashed(int x, int y, int w, int h, MfdColor color, int dashLen = 2);
    void drawLine(int x0, int y0, int x1, int y1, MfdColor color);
    void drawScanlines(MfdColor color, int step = 4);
    void drawChar(int x, int y, char c, MfdColor color, int scale = 1);
    void drawText(int x, int y, const std::string& text, MfdColor color, int scale = 1);
    void drawTextRight(int rightX, int y, const std::string& text, MfdColor color, int scale = 1);

    // Schema v1 primitives (§5.3, Ticket A8):
    void drawProgressBar(int x, int y, int width, int height, float value, float maxVal, int segments, MfdColor fillColor, MfdColor troughColor, MfdColor borderColor);
    void drawIcon(int x, int y, MfdIcon icon, MfdColor color, int size = 12);
    void drawCheck(int x, int y, const MfdCheckItem& check, MfdColor col, MfdColor dimCol, MfdColor bgCol, int size = 10);
    void drawChar3x5(int x, int y, char c, MfdColor color);
    void drawTextMini(int bx, int by, int bw, int bh, const std::string& text, MfdColor color);

private:
    int m_width;
    int m_height;
    MfdColorTheme m_theme = MfdColorTheme::kDefaultAmber;
    MfdPaletteSet m_activePalette;
    std::vector<uint32_t> m_pixels; // RGBA8 packed

    ComPtr<ID3D11Texture2D> m_d3dTexture;
    ComPtr<ID3D11ShaderResourceView> m_d3dSrv;
    int m_texWidth = 0;
    int m_texHeight = 0;

    void renderHeader(const MfdViewModel& model, MfdColor mainColor, MfdColor dimColor);
    void renderTabs(const MfdViewModel& model, MfdColor mainColor, MfdColor dimColor);
    void renderListTab(const MfdTab& tab, bool focused, MfdColor mainColor, MfdColor dimColor);
    void renderKeyValueTab(const MfdTab& tab, MfdColor mainColor, MfdColor dimColor);
    void renderTextTab(const MfdTab& tab, MfdColor mainColor, MfdColor dimColor);
    void renderBarsTab(const MfdTab& tab, bool focused, MfdColor mainColor, MfdColor dimColor);
    void renderFooter(const MfdViewModel& model, MfdColor dimColor);
};

} // namespace edvr::mfd
