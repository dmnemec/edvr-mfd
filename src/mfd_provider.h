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
    using TabChangeCallback = std::function<void(const std::string& tabId, int tabIndex)>;
    using NavCallback = std::function<void(const std::string& tabId, int itemIndex, const std::string& itemId, const std::string& itemLabel)>;
    using SelectCallback = std::function<void(const std::string& tabId, int itemIndex, const std::string& itemId, const std::string& itemLabel, const std::string& action, const std::string& value)>;
    // itemIndex is -1 and ids/labels empty when no item is focused (empty or text tab).
    using BackCallback = std::function<void(const std::string& tabId, int itemIndex, const std::string& itemId, const std::string& itemLabel, const std::string& action, const std::string& value)>;
    using AdjustCallback = std::function<void(const std::string& tabId, int itemIndex, const std::string& itemId, const std::string& itemLabel, const std::string& key, int delta)>;

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

    void setTabChangeCallback(TabChangeCallback cb) {
        m_tabChangeCallback = std::move(cb);
    }

    void setNavCallback(NavCallback cb) {
        m_navCallback = std::move(cb);
    }

    void setSelectCallback(SelectCallback cb) {
        m_selectCallback = std::move(cb);
    }

    void setBackCallback(BackCallback cb) {
        m_backCallback = std::move(cb);
    }

    void setAdjustCallback(AdjustCallback cb) {
        m_adjustCallback = std::move(cb);
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

        if (hasAction(action, MfdInputAction::kNextTab) || hasAction(action, MfdInputAction::kPrevTab)) {
            int oldTabIdx = m_model.activeTabIndex;
            if (hasAction(action, MfdInputAction::kNextTab)) {
                m_model.cycleNextTab();
            } else {
                m_model.cyclePrevTab();
            }
            if (m_model.activeTabIndex != oldTabIdx && m_tabChangeCallback) {
                auto* curTab = m_model.currentTab();
                if (curTab) {
                    try {
                        m_tabChangeCallback(curTab->id.empty() ? curTab->title : curTab->id, m_model.activeTabIndex);
                    } catch (...) {}
                }
            }
            return true;
        }
        if (hasAction(action, MfdInputAction::kUp) || hasAction(action, MfdInputAction::kDown)) {
            auto* tab = m_model.currentTab();
            int oldIdx = tab ? tab->selectedIndex : -1;
            int delta = hasAction(action, MfdInputAction::kUp) ? -1 : 1;
            m_model.moveSelection(delta);
            if (tab && tab->selectedIndex != oldIdx && m_navCallback) {
                std::string tabId = tab->id.empty() ? tab->title : tab->id;
                if (tab->type == MfdTabType::kList && tab->selectedIndex >= 0 && tab->selectedIndex < static_cast<int>(tab->items.size())) {
                    const auto& it = tab->items[tab->selectedIndex];
                    try { m_navCallback(tabId, tab->selectedIndex, it.id, it.label); } catch (...) {}
                } else if (tab->type == MfdTabType::kKeyValue && tab->selectedIndex >= 0 && tab->selectedIndex < static_cast<int>(tab->keyValues.size())) {
                    const auto& kv = tab->keyValues[tab->selectedIndex];
                    try { m_navCallback(tabId, tab->selectedIndex, kv.id, kv.key); } catch (...) {}
                } else if (tab->type == MfdTabType::kBars && tab->selectedIndex >= 0 && tab->selectedIndex < static_cast<int>(tab->barItems.size())) {
                    const auto& bar = tab->barItems[tab->selectedIndex];
                    try { m_navCallback(tabId, tab->selectedIndex, bar.id, bar.label); } catch (...) {}
                }
            }
            return true;
        }
        if (hasAction(action, MfdInputAction::kLeft) || hasAction(action, MfdInputAction::kRight)) {
            auto* tab = m_model.currentTab();
            if (tab && (tab->type == MfdTabType::kKeyValue || tab->type == MfdTabType::kBars)) {
                int delta = hasAction(action, MfdInputAction::kRight) ? 1 : -1;
                std::string tabId = tab->id.empty() ? tab->title : tab->id;
                if (tab->type == MfdTabType::kKeyValue && !tab->keyValues.empty() &&
                    tab->selectedIndex >= 0 && tab->selectedIndex < static_cast<int>(tab->keyValues.size())) {
                    const auto& kv = tab->keyValues[tab->selectedIndex];
                    if (m_adjustCallback) {
                        try { m_adjustCallback(tabId, tab->selectedIndex, kv.id, kv.key, kv.key, delta); } catch (...) {}
                    }
                    if (m_kvActionCallback) {
                        try { m_kvActionCallback(tab->title, tab->selectedIndex, kv.key, delta); } catch (...) {}
                    }
                    return true;
                } else if (tab->type == MfdTabType::kBars && !tab->barItems.empty() &&
                           tab->selectedIndex >= 0 && tab->selectedIndex < static_cast<int>(tab->barItems.size())) {
                    const auto& bar = tab->barItems[tab->selectedIndex];
                    if (m_adjustCallback) {
                        try { m_adjustCallback(tabId, tab->selectedIndex, bar.id, bar.label, bar.label, delta); } catch (...) {}
                    }
                    return true;
                }
            }
        }
        if (hasAction(action, MfdInputAction::kBack)) {
            // Back has no built-in navigation: report the press (with the focused item's backAction).
            auto* tab = m_model.currentTab();
            if (tab && m_backCallback) {
                std::string tabId = tab->id.empty() ? tab->title : tab->id;
                int sel = tab->selectedIndex;
                auto actStr = [](MfdItemAction a) { return a == MfdItemAction::kClipboard ? "clipboard" : "event"; };
                try {
                    if (tab->type == MfdTabType::kList && sel >= 0 && sel < static_cast<int>(tab->items.size())) {
                        const auto& item = tab->items[sel];
                        m_backCallback(tabId, sel, item.id, item.label, actStr(item.backAction), item.value);
                    } else if (tab->type == MfdTabType::kKeyValue && sel >= 0 && sel < static_cast<int>(tab->keyValues.size())) {
                        const auto& kv = tab->keyValues[sel];
                        m_backCallback(tabId, sel, kv.id, kv.key, actStr(kv.backAction), kv.value);
                    } else if (tab->type == MfdTabType::kBars && sel >= 0 && sel < static_cast<int>(tab->barItems.size())) {
                        const auto& bar = tab->barItems[sel];
                        m_backCallback(tabId, sel, bar.id, bar.label, "event", "");
                    } else {
                        m_backCallback(tabId, -1, "", "", "event", "");
                    }
                } catch (...) {}
            }
            return true;
        }
        if (hasAction(action, MfdInputAction::kSelect)) {
            auto* tab = m_model.currentTab();
            if (tab) {
                std::string tabId = tab->id.empty() ? tab->title : tab->id;
                if (tab->type == MfdTabType::kList && !tab->items.empty()) {
                    if (tab->selectedIndex >= 0 && tab->selectedIndex < static_cast<int>(tab->items.size())) {
                        const auto& item = tab->items[tab->selectedIndex];
                        std::string actionStr = (item.action == MfdItemAction::kClipboard) ? "clipboard" : "event";
                        if (m_selectCallback) {
                            try { m_selectCallback(tabId, tab->selectedIndex, item.id, item.label, actionStr, item.value); } catch (...) {}
                        }
                        if (m_actionCallback) {
                            try { m_actionCallback(item.id, item.label); } catch (...) {}
                        }
                        return true;
                    }
                } else if (tab->type == MfdTabType::kKeyValue && !tab->keyValues.empty()) {
                    if (tab->selectedIndex >= 0 && tab->selectedIndex < static_cast<int>(tab->keyValues.size())) {
                        const auto& kv = tab->keyValues[tab->selectedIndex];
                        if (m_selectCallback) {
                            try { m_selectCallback(tabId, tab->selectedIndex, kv.id, kv.key, "event", kv.value); } catch (...) {}
                        }
                        if (m_kvActionCallback) {
                            try { m_kvActionCallback(tab->title, tab->selectedIndex, kv.key, 1); } catch (...) {}
                        }
                        return true;
                    }
                } else if (tab->type == MfdTabType::kBars && !tab->barItems.empty()) {
                    if (tab->selectedIndex >= 0 && tab->selectedIndex < static_cast<int>(tab->barItems.size())) {
                        const auto& bar = tab->barItems[tab->selectedIndex];
                        if (m_selectCallback) {
                            try { m_selectCallback(tabId, tab->selectedIndex, bar.id, bar.label, "event", ""); } catch (...) {}
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
    TabChangeCallback m_tabChangeCallback;
    NavCallback m_navCallback;
    SelectCallback m_selectCallback;
    BackCallback m_backCallback;
    AdjustCallback m_adjustCallback;
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
