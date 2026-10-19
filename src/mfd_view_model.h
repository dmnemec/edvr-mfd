#pragma once

#include "mfd_types.h"
#include "mfd_screen_size.h"
#include <string>
#include <vector>
#include <cstdint>

namespace edvr::mfd {

// Check state for list item checks
enum class MfdCheckState : uint8_t {
    kDone,
    kTodo,
    kNa
};

struct MfdCheckItem {
    std::string label;        // 1-2 chars
    MfdCheckState state = MfdCheckState::kTodo;
};

// Fixed glyph set for icons (§5.3)
enum class MfdIcon : uint8_t {
    kNone = 0,
    kFuel,
    kWarning,
    kNeutron,
    kWhitedwarf,
    kBio,
    kGeo,
    kCheck,
    kLock,
    kStar
};

// Item action trigger type
enum class MfdItemAction : uint8_t {
    kEvent = 0,
    kClipboard = 1
};

// Named palette colors (§5.3)
enum class MfdNamedColor : uint8_t {
    kNormal = 0,
    kDim,
    kAccent,
    kOk,
    kWarn,
    kCrit
};

// Bar gauge item within a bars tab
struct MfdBarItem {
    std::string id;           // Unique within tab
    std::string label;
    float value = 0.0f;
    float max = 1.0f;
    int segments = 0;
    std::string colorName = "normal";
    MfdNamedColor namedColor = MfdNamedColor::kNormal;
    MfdColor resolvedColor = palette::kAmberNormal;
};

// Individual item within a list tab.
struct MfdListItem {
    std::string id;
    std::string label;        // Main text (e.g. system name, commodity name)
    std::string value;        // Right-aligned value (e.g. "45.2 LY", "12,450 CR")
    std::string sublabel;     // Secondary detail (e.g. "Class M Red Dwarf", "Demand: High")
    std::string badge;        // Status tag (e.g. "[REFUEL]", "[NEXT]", "[HOT]")
    MfdColor badgeColor = palette::kCyanAccent;
    bool isSelectable = true;

    // Schema v1 fields:
    std::string valueColorName = "normal";
    MfdNamedColor valueNamedColor = MfdNamedColor::kNormal;
    MfdColor valueColor = palette::kAmberNormal;
    std::string iconName;
    MfdIcon icon = MfdIcon::kNone;
    std::vector<MfdCheckItem> checks;
    MfdItemAction action = MfdItemAction::kEvent;
    MfdItemAction backAction = MfdItemAction::kEvent;  // Run on Back press

    MfdListItem() = default;
    MfdListItem(std::string id_, std::string label_, std::string value_ = "", std::string sublabel_ = "", std::string badge_ = "")
        : id(std::move(id_)), label(std::move(label_)), value(std::move(value_)),
          sublabel(std::move(sublabel_)), badge(std::move(badge_)) {}
};

// Key-value entry in a status/summary grid.
struct MfdKeyValue {
    std::string key;
    std::string value;
    MfdColor valueColor = palette::kAmberNormal;

    // Schema v1 fields:
    std::string id;
    std::string valueColorName = "normal";
    MfdNamedColor valueNamedColor = MfdNamedColor::kNormal;
    MfdItemAction action = MfdItemAction::kEvent;
    MfdItemAction backAction = MfdItemAction::kEvent;  // Run on Back press

    MfdKeyValue() = default;
    MfdKeyValue(std::string k, std::string v, MfdColor vc = palette::kAmberNormal)
        : key(std::move(k)), value(std::move(v)), valueColor(vc), id(key) {}
    MfdKeyValue(std::string id_, std::string k, std::string v, MfdColor vc = palette::kAmberNormal)
        : key(std::move(k)), value(std::move(v)), valueColor(vc), id(std::move(id_)) {}
};

// Tab type definition.
enum class MfdTabType : uint8_t {
    kList = 0,       // Scrollable list of actionable items
    kKeyValue = 1,   // Status grid of metrics / stats
    kText = 2,       // Scrollable text log / briefing
    kBars = 3        // Bar indicators / progress gauges
};

// Definition of a single MFD tab.
struct MfdTab {
    std::string id;
    std::string title;
    MfdTabType type = MfdTabType::kList;

    // For kList tabs:
    std::vector<MfdListItem> items;
    int selectedIndex = 0;
    int scrollOffset = 0;

    // For kKeyValue tabs:
    std::vector<MfdKeyValue> keyValues;

    // For kText tabs:
    std::vector<std::string> lines;
    int textScrollLine = 0;

    // For kBars tabs:
    std::vector<MfdBarItem> barItems;

    // Rows the standard renderer shows for this tab at renderHeight (#14). 0 = height
    // unknown (e.g. a model not attached to a screen): the pre-#4 fixed counts.
    int visibleRows(int renderHeight) const {
        switch (type) {
            case MfdTabType::kList: {
                if (renderHeight <= 0) return 8;
                bool hasAnySublabel = false;
                for (const auto& item : items) {
                    if (!item.sublabel.empty()) { hasAnySublabel = true; break; }
                }
                return standardVisibleRows(renderHeight, kStdContentTop,
                                           hasAnySublabel ? kStdListSublabelRowHeight : kStdListRowHeight);
            }
            case MfdTabType::kKeyValue:
                if (renderHeight <= 0) return 11;
                return standardVisibleRows(renderHeight, kStdKeyValueTop, kStdKeyValueRowHeight);
            case MfdTabType::kBars:
                if (renderHeight <= 0) return 8;
                return standardVisibleRows(renderHeight, kStdContentTop, kStdBarRowHeight);
            default:
                return 1;
        }
    }

    // Scroll just enough that selectedIndex is inside the visible window.
    void keepSelectionVisible(int rows) {
        if (selectedIndex < scrollOffset) {
            scrollOffset = selectedIndex;
        } else if (selectedIndex >= scrollOffset + rows) {
            scrollOffset = selectedIndex - rows + 1;
        }
    }
};

// Complete structured view model representing one MFD screen.
struct MfdViewModel {
    // Schema version:
    int schemaVersion = 1;

    // Header section:
    std::string title = "ELITE AVIONICS";
    std::string subtitle;
    std::string statusBadge = "ONLINE";
    std::string statusBadgeColorName = "ok";
    MfdNamedColor statusBadgeNamedColor = MfdNamedColor::kOk;
    MfdColor statusBadgeColor = palette::kSuccessGreen;

    // Tabs navigation:
    std::vector<MfdTab> tabs;
    int activeTabIndex = 0;

    // Footer section:
    std::string footerHint = "[Q/E] TABS   [\x1E/\x1F] NAVIGATE   [SPACE] SELECT";
    std::string paginationText;

    // Focus appearance:
    bool isFocused = false;

    // Render height of the screen showing this model, kept current by MfdManager (0 = unknown).
    int renderHeight = 0;

    // Helper accessors:
    MfdTab* currentTab() {
        if (activeTabIndex >= 0 && activeTabIndex < static_cast<int>(tabs.size())) {
            return &tabs[activeTabIndex];
        }
        return nullptr;
    }

    const MfdTab* currentTab() const {
        if (activeTabIndex >= 0 && activeTabIndex < static_cast<int>(tabs.size())) {
            return &tabs[activeTabIndex];
        }
        return nullptr;
    }

    void cycleNextTab() {
        if (tabs.empty()) return;
        activeTabIndex = (activeTabIndex + 1) % static_cast<int>(tabs.size());
    }

    void cyclePrevTab() {
        if (tabs.empty()) return;
        activeTabIndex = (activeTabIndex - 1 + static_cast<int>(tabs.size())) % static_cast<int>(tabs.size());
    }

    void moveSelection(int delta) {
        auto* tab = currentTab();
        if (!tab) return;

        if (tab->type == MfdTabType::kList && !tab->items.empty()) {
            int newIdx = tab->selectedIndex + delta;
            if (newIdx < 0) newIdx = 0;
            if (newIdx >= static_cast<int>(tab->items.size())) {
                newIdx = static_cast<int>(tab->items.size()) - 1;
            }
            tab->selectedIndex = newIdx;
            tab->keepSelectionVisible(tab->visibleRows(renderHeight));
        } else if (tab->type == MfdTabType::kKeyValue && !tab->keyValues.empty()) {
            int newIdx = tab->selectedIndex + delta;
            if (newIdx < 0) newIdx = 0;
            if (newIdx >= static_cast<int>(tab->keyValues.size())) {
                newIdx = static_cast<int>(tab->keyValues.size()) - 1;
            }
            tab->selectedIndex = newIdx;
            tab->keepSelectionVisible(tab->visibleRows(renderHeight));
        } else if (tab->type == MfdTabType::kBars && !tab->barItems.empty()) {
            int newIdx = tab->selectedIndex + delta;
            if (newIdx < 0) newIdx = 0;
            if (newIdx >= static_cast<int>(tab->barItems.size())) {
                newIdx = static_cast<int>(tab->barItems.size()) - 1;
            }
            tab->selectedIndex = newIdx;
            tab->keepSelectionVisible(tab->visibleRows(renderHeight));
        } else if (tab->type == MfdTabType::kText && !tab->lines.empty()) {
            int newScroll = tab->textScrollLine + delta;
            if (newScroll < 0) newScroll = 0;
            if (newScroll >= static_cast<int>(tab->lines.size())) {
                newScroll = static_cast<int>(tab->lines.size()) - 1;
            }
            tab->textScrollLine = newScroll;
        }
    }
};

} // namespace edvr::mfd
