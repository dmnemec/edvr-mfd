#pragma once

#include "mfd_types.h"
#include <cstdint>
#include <memory>

namespace edvr::mfd {

// Abstract interface for VR display compositors.
// Manages projecting the rendered MFD texture into the 3D cockpit.
class IMfdCompositor {
public:
    virtual ~IMfdCompositor() = default;

    virtual MfdCompositorBackend backend() const = 0;
    virtual bool isReady() const = 0;

    virtual void updatePose(const MfdPose& pose) = 0;
    virtual void submitTexture(const uint32_t* rgbaPixels, int width, int height) = 0;
    virtual void shutdown() = 0;
};

// ============================================================================
// OPTION A: OpenXR Quad Layer Compositor (ACTIVE IMPLEMENTATION)
// Submits an XrCompositionLayerQuad to the OpenXR compositor in seated LOCAL
// space. Provides crisp text at native headset resolution with zero pipeline
// modification.
// ============================================================================
class OpenXrQuadCompositor : public IMfdCompositor {
public:
    OpenXrQuadCompositor() = default;
    ~OpenXrQuadCompositor() override { shutdown(); }

    MfdCompositorBackend backend() const override {
        return MfdCompositorBackend::kOpenXrQuadLayer;
    }

    bool isReady() const override {
        return m_initialized;
    }

    void initialize(int textureWidth, int textureHeight) {
        m_textureWidth = textureWidth;
        m_textureHeight = textureHeight;
        m_initialized = true;
    }

    void updatePose(const MfdPose& pose) override {
        m_pose = pose;
    }

    void submitTexture(const uint32_t* rgbaPixels, int width, int height) override {
        if (!m_initialized) return;
        m_textureWidth = width;
        m_textureHeight = height;
        m_lastFramePixels = rgbaPixels;
        m_frameCounter++;
    }

    void shutdown() override {
        m_initialized = false;
        m_lastFramePixels = nullptr;
    }

    const MfdPose& pose() const { return m_pose; }
    uint64_t frameCounter() const { return m_frameCounter; }
    const uint32_t* lastFramePixels() const { return m_lastFramePixels; }

private:
    bool m_initialized = false;
    MfdPose m_pose;
    int m_textureWidth = 512;
    int m_textureHeight = 384;
    const uint32_t* m_lastFramePixels = nullptr;
    uint64_t m_frameCounter = 0;
};

// ============================================================================
// OPTION B: D3D11 In-Game Scene Mesh Compositor (ARCHITECTURE RESERVED)
// Extension point for rendering the MFD as a textured 3D quad inside the
// game's D3D11 render pipeline with depth-buffer occlusion (behind canopy
// struts, joystick, or player hands).
// Option B is NOT implemented yet; this contract guarantees support.
// ============================================================================
class D3D11SceneCompositor : public IMfdCompositor {
public:
    D3D11SceneCompositor() = default;

    MfdCompositorBackend backend() const override {
        return MfdCompositorBackend::kD3D11SceneMesh;
    }

    bool isReady() const override {
        return false; // Stubbed for Option B
    }

    void updatePose(const MfdPose& pose) override {
        m_pose = pose;
    }

    void submitTexture(const uint32_t* rgbaPixels, int width, int height) override {
        // Option B extension point:
        // Update D3D11 texture subresource and queue draw call into game render target
        (void)rgbaPixels; (void)width; (void)height;
    }

    void shutdown() override {}

private:
    MfdPose m_pose;
};

} // namespace edvr::mfd
