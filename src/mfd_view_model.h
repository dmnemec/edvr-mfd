#pragma once

#include "mfd_types.h"
#include <string>
#include <vector>
#include <cstdint>

namespace edvr::mfd {

// Individual item within a list tab.
struct MfdListItem {
    std::string id;
    std::string label;        // Main text (e.g. system name, commodity name)
    std::string value;        // Right-aligned value (e.g. "45.2 LY", "12,450 CR")
    std::string sublabel;     // Secondary detail (e.g. "Class M Red Dwarf", "Demand: High")
    std::string badge;        // Status tag (e.g. "[REFUEL]", "[NEXT]", "[HOT]")
    MfdColor badgeColor = palette::kCyanAccent;
    bool isSelectable = true;
};

// Key-value entry in a status/summary grid.
struct MfdKeyValue {
    std::string key;
    std::string value;
    MfdColor valueColor = palette::kAmberNormal;
};

// Tab type definition.
enum class MfdTabType : uint8_t {
    kList = 0,       // Scrollable list of actionable items
    kKeyValue = 1,   // Status grid of metrics / stats
    kText = 2        // Scrollable text log / briefing
};

// Definition of a single MFD tab.
struct MfdTab {
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
};

// Complete structured view model representing one MFD screen.
struct MfdViewModel {
    // Header section:
    std::string title = "ELITE AVIONICS";
    std::string subtitle;
    std::string statusBadge = "ONLINE";
    MfdColor statusBadgeColor = palette::kSuccessGreen;

    // Tabs navigation:
    std::vector<MfdTab> tabs;
    int activeTabIndex = 0;

    // Footer section:
    std::string footerHint = "[Q/E] TABS   [\x1E/\x1F] NAVIGATE   [SPACE] SELECT";
    std::string paginationText;

    // Focus appearance:
    bool isFocused = false;

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

    void moveSelection(int delta, int visibleRows = 8) {
        auto* tab = currentTab();
        if (!tab) return;

        if (tab->type == MfdTabType::kList && !tab->items.empty()) {
            int newIdx = tab->selectedIndex + delta;
            if (newIdx < 0) newIdx = 0;
            if (newIdx >= static_cast<int>(tab->items.size())) {
                newIdx = static_cast<int>(tab->items.size()) - 1;
            }
            tab->selectedIndex = newIdx;

            // Ensure selection is visible within viewport
            if (tab->selectedIndex < tab->scrollOffset) {
                tab->scrollOffset = tab->selectedIndex;
            } else if (tab->selectedIndex >= tab->scrollOffset + visibleRows) {
                tab->scrollOffset = tab->selectedIndex - visibleRows + 1;
            }
        } else if (tab->type == MfdTabType::kKeyValue && !tab->keyValues.empty()) {
            int newIdx = tab->selectedIndex + delta;
            if (newIdx < 0) newIdx = 0;
            if (newIdx >= static_cast<int>(tab->keyValues.size())) {
                newIdx = static_cast<int>(tab->keyValues.size()) - 1;
            }
            tab->selectedIndex = newIdx;

            // Ensure selection is visible within viewport
            int kvVisibleRows = 11;
            if (tab->selectedIndex < tab->scrollOffset) {
                tab->scrollOffset = tab->selectedIndex;
            } else if (tab->selectedIndex >= tab->scrollOffset + kvVisibleRows) {
                tab->scrollOffset = tab->selectedIndex - kvVisibleRows + 1;
            }
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
