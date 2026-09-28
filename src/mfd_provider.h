#pragma once

#include "mfd_types.h"
#include "mfd_view_model.h"
#include <memory>
#include <string>
#include <functional>
#include <sstream>

namespace edvr::mfd {

// Abstract base interface for MFD content providers.
// Both Option A (Declarative JSON) and Option B (Scripted Code) implement this interface.
class IMfdProvider {
public:
    virtual ~IMfdProvider() = default;

    // Identifies the strategy: kDeclarativeJson (Option A) or kScriptedCode (Option B)
    virtual MfdProviderType type() const = 0;

    // Unique identifier for the provider (e.g. "spansh_router", "inara_market")
    virtual const char* id() const = 0;

    // Human-readable title
    virtual const char* title() const = 0;

    // Periodic tick for data polling, animations, or timer updates
    virtual void update(float dtSeconds) = 0;

    // Handle UI navigation input when focused.
    // Returns true if the action was consumed by the provider.
    virtual bool onInput(MfdInputAction action) = 0;

    // Notification of head-gaze focus changes
    virtual void onFocusChanged(bool focused) = 0;

    // Current structured view model for rendering
    virtual const MfdViewModel& viewModel() const = 0;
    virtual MfdViewModel& viewModel() = 0;
};

// ============================================================================
// OPTION A: Declarative JSON MFD Provider (ACTIVE IMPLEMENTATION)
// Loads tabs, lists, tables, and metrics from structured JSON definitions
// supplied via local files, named pipes, or REST/WebSocket endpoints.
// ============================================================================
class DeclarativeMfdProvider : public IMfdProvider {
public:
    using ActionCallback = std::function<void(const std::string& itemId, const std::string& itemLabel)>;
    using KeyValueActionCallback = std::function<void(const std::string& tabTitle, int index, const std::string& key, int adjustDelta)>;

    explicit DeclarativeMfdProvider(std::string id, std::string defaultTitle = "NAV MFD")
        : m_id(std::move(id)) {
        m_model.title = std::move(defaultTitle);
    }

    MfdProviderType type() const override {
        return MfdProviderType::kDeclarativeJson;
    }

    const char* id() const override {
        return m_id.c_str();
    }

    const char* title() const override {
        return m_model.title.c_str();
    }

    using UpdateHook = std::function<void(MfdViewModel& model, float dtSeconds)>;

    void setActionCallback(ActionCallback callback) {
        m_actionCallback = std::move(callback);
    }

    void setKeyValueActionCallback(KeyValueActionCallback callback) {
        m_kvActionCallback = std::move(callback);
    }

    void setUpdateHook(UpdateHook hook) {
        m_updateHook = std::move(hook);
    }

    void onFocusChanged(bool focused) override {
        m_model.isFocused = focused;
    }

    void update(float dtSeconds) override {
        if (m_updateHook) {
            m_updateHook(m_model, dtSeconds);
        }
    }

    bool onInput(MfdInputAction action) override {
        if (!m_model.isFocused) return false;

        if (hasAction(action, MfdInputAction::kNextTab)) {
            m_model.cycleNextTab();
            return true;
        }
        if (hasAction(action, MfdInputAction::kPrevTab)) {
            m_model.cyclePrevTab();
            return true;
        }
        if (hasAction(action, MfdInputAction::kUp)) {
            m_model.moveSelection(-1);
            return true;
        }
        if (hasAction(action, MfdInputAction::kDown)) {
            m_model.moveSelection(1);
            return true;
        }
        if (hasAction(action, MfdInputAction::kLeft) || hasAction(action, MfdInputAction::kRight)) {
            auto* tab = m_model.currentTab();
            if (tab && tab->type == MfdTabType::kKeyValue && !tab->keyValues.empty()) {
                if (tab->selectedIndex >= 0 && tab->selectedIndex < static_cast<int>(tab->keyValues.size())) {
                    int delta = hasAction(action, MfdInputAction::kRight) ? 1 : -1;
                    if (m_kvActionCallback) {
                        m_kvActionCallback(tab->title, tab->selectedIndex, tab->keyValues[tab->selectedIndex].key, delta);
                    }
                    return true;
                }
            }
        }
        if (hasAction(action, MfdInputAction::kSelect)) {
            auto* tab = m_model.currentTab();
            if (tab) {
                if (tab->type == MfdTabType::kList && !tab->items.empty()) {
                    if (tab->selectedIndex >= 0 && tab->selectedIndex < static_cast<int>(tab->items.size())) {
                        const auto& item = tab->items[tab->selectedIndex];
                        if (m_actionCallback) {
                            m_actionCallback(item.id, item.label);
                        }
                        return true;
                    }
                } else if (tab->type == MfdTabType::kKeyValue && !tab->keyValues.empty()) {
                    if (tab->selectedIndex >= 0 && tab->selectedIndex < static_cast<int>(tab->keyValues.size())) {
                        if (m_kvActionCallback) {
                            m_kvActionCallback(tab->title, tab->selectedIndex, tab->keyValues[tab->selectedIndex].key, 1);
                        }
                        return true;
                    }
                }
            }
        }

        return false;
    }

    const MfdViewModel& viewModel() const override {
        return m_model;
    }

    MfdViewModel& viewModel() override {
        return m_model;
    }

    // Parse and load state from a declarative JSON payload.
    // Handles title, subtitle, status badge, tabs, and list/key-value items.
    bool loadFromJson(const std::string& json);

private:
    std::string m_id;
    MfdViewModel m_model;
    ActionCallback m_actionCallback;
    KeyValueActionCallback m_kvActionCallback;
    UpdateHook m_updateHook;
};

// ============================================================================
// OPTION B: Scripted / Programmable Code MFD Provider (ARCHITECTURE RESERVED)
// This class provides the extension point for a future embedded scripting
// engine (e.g. Lua, WebAssembly, or a native C++ plugin DLL).
// Option B is NOT implemented yet, but this contract guarantees that the
// core MFD framework can admit Option B without architectural redesign.
// ============================================================================
class ScriptedMfdProvider : public IMfdProvider {
public:
    explicit ScriptedMfdProvider(std::string id, std::string scriptPath = "")
        : m_id(std::move(id)), m_scriptPath(std::move(scriptPath)) {
        m_model.title = "SCRIPTED MFD";
        m_model.subtitle = "OPTION B ENGINE (STANDBY)";
        m_model.statusBadge = "STUB";
        m_model.statusBadgeColor = palette::kAmberDim;
    }

    MfdProviderType type() const override {
        return MfdProviderType::kScriptedCode;
    }

    const char* id() const override {
        return m_id.c_str();
    }

    const char* title() const override {
        return m_model.title.c_str();
    }

    const std::string& scriptPath() const {
        return m_scriptPath;
    }

    void onFocusChanged(bool focused) override {
        m_model.isFocused = focused;
        // Option B hook: notify script environment of focus change
        // e.g. lua_getglobal(L, "on_focus"); lua_pushboolean(L, focused); lua_pcall(L, 1, 0, 0);
    }

    void update(float dtSeconds) override {
        // Option B hook: tick script environment
        // e.g. lua_getglobal(L, "on_update"); lua_pushnumber(L, dtSeconds); lua_pcall(L, 1, 0, 0);
        (void)dtSeconds;
    }

    bool onInput(MfdInputAction action) override {
        if (!m_model.isFocused) return false;

        // Option B hook: pass input action directly to script callback
        // e.g. lua_getglobal(L, "on_input"); lua_pushinteger(L, static_cast<int>(action)); ...
        // Default fallback handling:
        if (hasAction(action, MfdInputAction::kNextTab)) { m_model.cycleNextTab(); return true; }
        if (hasAction(action, MfdInputAction::kPrevTab)) { m_model.cyclePrevTab(); return true; }
        if (hasAction(action, MfdInputAction::kUp)) { m_model.moveSelection(-1); return true; }
        if (hasAction(action, MfdInputAction::kDown)) { m_model.moveSelection(1); return true; }

        return false;
    }

    const MfdViewModel& viewModel() const override {
        return m_model;
    }

    MfdViewModel& viewModel() override {
        return m_model;
    }

private:
    std::string m_id;
    std::string m_scriptPath;
    MfdViewModel m_model;
};

} // namespace edvr::mfd
