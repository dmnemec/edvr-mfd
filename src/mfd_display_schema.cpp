#include "mfd_display_schema.h"
#include "log.h"
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <algorithm>
#include <cctype>

namespace edvr::mfd {

namespace {

constexpr size_t kMaxFileSize = 1024 * 1024; // 1 MB
constexpr size_t kMaxStringLength = 128;
constexpr size_t kMaxTabs = 16;
constexpr size_t kMaxItemsPerTab = 256;
constexpr size_t kMaxChecksPerItem = 6;

std::string truncateString(const std::string& str, size_t maxLen = kMaxStringLength) {
    if (str.size() <= maxLen) return str;
    return str.substr(0, maxLen);
}

void computeLineCol(const char* data, size_t length, size_t byteOffset, int& outLine, int& outCol) {
    outLine = 1;
    outCol = 1;
    size_t limit = (byteOffset < length) ? byteOffset : length;
    for (size_t i = 0; i < limit; ++i) {
        if (data[i] == '\n') {
            outLine++;
            outCol = 1;
        } else {
            outCol++;
        }
    }
}

void findTokenLocation(const char* data, size_t length, const std::string& token, int& outLine, int& outCol) {
    if (token.empty()) {
        outLine = 1;
        outCol = 1;
        return;
    }
    std::string_view sv(data, length);
    size_t pos = sv.find(token);
    if (pos != std::string_view::npos) {
        computeLineCol(data, length, pos, outLine, outCol);
    } else {
        outLine = 1;
        outCol = 1;
    }
}

} // namespace

MfdNamedColor parseNamedColor(const std::string& name) {
    if (name == "normal") return MfdNamedColor::kNormal;
    if (name == "dim")    return MfdNamedColor::kDim;
    if (name == "accent") return MfdNamedColor::kAccent;
    if (name == "ok")     return MfdNamedColor::kOk;
    if (name == "warn")   return MfdNamedColor::kWarn;
    if (name == "crit")   return MfdNamedColor::kCrit;

    static std::unordered_set<std::string> s_warnedColors;
    if (!name.empty() && s_warnedColors.find(name) == s_warnedColors.end()) {
        s_warnedColors.insert(name);
        Log::get().note("mfd_schema: Unknown named color '%s', defaulting to 'normal'\n", name.c_str());
    }
    return MfdNamedColor::kNormal;
}

const char* namedColorToString(MfdNamedColor color) {
    switch (color) {
        case MfdNamedColor::kNormal: return "normal";
        case MfdNamedColor::kDim:    return "dim";
        case MfdNamedColor::kAccent: return "accent";
        case MfdNamedColor::kOk:     return "ok";
        case MfdNamedColor::kWarn:   return "warn";
        case MfdNamedColor::kCrit:   return "crit";
        default:                     return "normal";
    }
}

MfdColor resolveNamedColor(MfdNamedColor color, const MfdPaletteSet& theme) {
    switch (color) {
        case MfdNamedColor::kNormal: return theme.mainColor;
        case MfdNamedColor::kDim:    return theme.dimColor;
        case MfdNamedColor::kAccent: return theme.accentColor;
        case MfdNamedColor::kOk:     return palette::kSuccessGreen;
        case MfdNamedColor::kWarn:   return palette::kAmberBright;
        case MfdNamedColor::kCrit:   return palette::kAlertRed;
        default:                     return theme.mainColor;
    }
}

MfdColor resolveNamedColor(const std::string& name, const MfdPaletteSet& theme) {
    return resolveNamedColor(parseNamedColor(name), theme);
}

MfdIcon parseIcon(const std::string& name) {
    if (name.empty()) return MfdIcon::kNone;
    if (name == "fuel")       return MfdIcon::kFuel;
    if (name == "warning")    return MfdIcon::kWarning;
    if (name == "neutron")    return MfdIcon::kNeutron;
    if (name == "whitedwarf") return MfdIcon::kWhitedwarf;
    if (name == "bio")        return MfdIcon::kBio;
    if (name == "geo")        return MfdIcon::kGeo;
    if (name == "check")      return MfdIcon::kCheck;
    if (name == "lock")       return MfdIcon::kLock;
    if (name == "star")       return MfdIcon::kStar;

    static std::unordered_set<std::string> s_warnedIcons;
    if (s_warnedIcons.find(name) == s_warnedIcons.end()) {
        s_warnedIcons.insert(name);
        Log::get().note("mfd_schema: Unknown icon '%s' ignored\n", name.c_str());
    }
    return MfdIcon::kNone;
}

const char* iconToString(MfdIcon icon) {
    switch (icon) {
        case MfdIcon::kFuel:       return "fuel";
        case MfdIcon::kWarning:    return "warning";
        case MfdIcon::kNeutron:    return "neutron";
        case MfdIcon::kWhitedwarf: return "whitedwarf";
        case MfdIcon::kBio:        return "bio";
        case MfdIcon::kGeo:        return "geo";
        case MfdIcon::kCheck:      return "check";
        case MfdIcon::kLock:       return "lock";
        case MfdIcon::kStar:       return "star";
        case MfdIcon::kNone:
        default:                   return "";
    }
}

MfdCheckState parseCheckState(const std::string& state) {
    if (state == "done") return MfdCheckState::kDone;
    if (state == "na")   return MfdCheckState::kNa;
    return MfdCheckState::kTodo;
}

const char* checkStateToString(MfdCheckState state) {
    switch (state) {
        case MfdCheckState::kDone: return "done";
        case MfdCheckState::kNa:   return "na";
        case MfdCheckState::kTodo:
        default:                   return "todo";
    }
}

MfdItemAction parseItemAction(const std::string& action) {
    if (action == "clipboard") return MfdItemAction::kClipboard;
    return MfdItemAction::kEvent;
}

const char* itemActionToString(MfdItemAction action) {
    switch (action) {
        case MfdItemAction::kClipboard: return "clipboard";
        case MfdItemAction::kEvent:
        default:                        return "event";
    }
}

bool parseDisplayJson(const std::string& jsonText,
                      MfdViewModel& outVm,
                      MfdParseError* outError,
                      const MfdPaletteSet* palette) {
    return parseDisplayJson(jsonText.data(), jsonText.size(), outVm, outError, palette);
}

bool parseDisplayJson(const char* data,
                      size_t length,
                      MfdViewModel& outVm,
                      MfdParseError* outError,
                      const MfdPaletteSet* palette) {
    if (!data || length == 0) {
        if (outError) {
            outError->line = 1;
            outError->column = 1;
            outError->message = "Empty JSON payload";
        }
        return false;
    }

    if (length > kMaxFileSize) {
        if (outError) {
            outError->line = 1;
            outError->column = 1;
            outError->message = "File exceeds 1 MB limit";
        }
        return false;
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(data, data + length);
    } catch (const nlohmann::json::parse_error& e) {
        if (outError) {
            size_t errByte = (e.byte > 0) ? (e.byte - 1) : 0;
            computeLineCol(data, length, errByte, outError->line, outError->column);
            outError->message = e.what();
        }
        return false;
    }

    if (!root.is_object()) {
        if (outError) {
            outError->line = 1;
            outError->column = 1;
            outError->message = "Root JSON must be an object";
        }
        return false;
    }

    // 1. Validate schemaVersion == 1 (§5.3: Major != 1 -> refuse + load_error)
    if (!root.contains("schemaVersion")) {
        if (outError) {
            findTokenLocation(data, length, "schemaVersion", outError->line, outError->column);
            outError->message = "Missing required 'schemaVersion'";
        }
        return false;
    }

    int majorVersion = -1;
    const auto& verNode = root["schemaVersion"];
    if (verNode.is_number_integer()) {
        majorVersion = verNode.get<int>();
    } else if (verNode.is_string()) {
        try {
            majorVersion = std::stoi(verNode.get<std::string>());
        } catch (...) {
            majorVersion = -1;
        }
    }

    if (majorVersion != 1) {
        if (outError) {
            findTokenLocation(data, length, "schemaVersion", outError->line, outError->column);
            outError->message = "Unsupported schemaVersion: expected 1";
        }
        return false;
    }

    MfdPaletteSet activeTheme = palette ? *palette : palette::getTheme(MfdColorTheme::kDefaultAmber);

    MfdViewModel vm;
    vm.schemaVersion = 1;

    // Header properties
    if (root.contains("title") && root["title"].is_string()) {
        vm.title = truncateString(root["title"].get<std::string>());
    }
    if (root.contains("subtitle") && root["subtitle"].is_string()) {
        vm.subtitle = truncateString(root["subtitle"].get<std::string>());
    }
    if (root.contains("footerHint") && root["footerHint"].is_string()) {
        vm.footerHint = truncateString(root["footerHint"].get<std::string>());
    }
    if (root.contains("paginationText") && root["paginationText"].is_string()) {
        vm.paginationText = truncateString(root["paginationText"].get<std::string>());
    }

    // statusBadge: object {text, color} or string
    if (root.contains("statusBadge")) {
        const auto& badgeVal = root["statusBadge"];
        if (badgeVal.is_object()) {
            if (badgeVal.contains("text") && badgeVal["text"].is_string()) {
                vm.statusBadge = truncateString(badgeVal["text"].get<std::string>());
            }
            if (badgeVal.contains("color") && badgeVal["color"].is_string()) {
                vm.statusBadgeColorName = badgeVal["color"].get<std::string>();
                vm.statusBadgeNamedColor = parseNamedColor(vm.statusBadgeColorName);
                vm.statusBadgeColor = resolveNamedColor(vm.statusBadgeNamedColor, activeTheme);
            } else {
                vm.statusBadgeColorName = "ok";
                vm.statusBadgeNamedColor = MfdNamedColor::kOk;
                vm.statusBadgeColor = resolveNamedColor(MfdNamedColor::kOk, activeTheme);
            }
        } else if (badgeVal.is_string()) {
            vm.statusBadge = truncateString(badgeVal.get<std::string>());
            vm.statusBadgeColorName = "ok";
            vm.statusBadgeNamedColor = MfdNamedColor::kOk;
            vm.statusBadgeColor = resolveNamedColor(MfdNamedColor::kOk, activeTheme);
        }
    }

    // Tabs array
    if (root.contains("tabs")) {
        if (!root["tabs"].is_array()) {
            if (outError) {
                findTokenLocation(data, length, "tabs", outError->line, outError->column);
                outError->message = "'tabs' field must be an array";
            }
            return false;
        }

        const auto& tabsArr = root["tabs"];
        size_t tabCount = tabsArr.size();
        if (tabCount > kMaxTabs) {
            static bool s_warnedTabOverflow = false;
            if (!s_warnedTabOverflow) {
                s_warnedTabOverflow = true;
                Log::get().note("mfd_schema: Tab count %zu exceeds limit %zu, truncating\n", tabCount, kMaxTabs);
            }
            tabCount = kMaxTabs;
        }

        std::unordered_set<std::string> tabIds;

        for (size_t tIdx = 0; tIdx < tabCount; ++tIdx) {
            const auto& tabVal = tabsArr[tIdx];
            if (!tabVal.is_object()) {
                if (outError) {
                    outError->line = 1;
                    outError->column = 1;
                    outError->message = "Tab element must be a JSON object";
                }
                return false;
            }

            // Tab id is required and unique within screen
            if (!tabVal.contains("id") || !tabVal["id"].is_string() || tabVal["id"].get<std::string>().empty()) {
                if (outError) {
                    findTokenLocation(data, length, tabVal.dump(), outError->line, outError->column);
                    outError->message = "Tab missing required 'id'";
                }
                return false;
            }

            std::string tabId = tabVal["id"].get<std::string>();
            if (tabId == "__settings") {
                if (outError) {
                    findTokenLocation(data, length, "__settings", outError->line, outError->column);
                    outError->message = "Tab id '__settings' is reserved for system use";
                }
                return false;
            }

            if (tabIds.find(tabId) != tabIds.end()) {
                if (outError) {
                    findTokenLocation(data, length, tabId, outError->line, outError->column);
                    outError->message = "Duplicate tab id: '" + tabId + "'";
                }
                return false;
            }
            tabIds.insert(tabId);

            MfdTab tab;
            tab.id = tabId;
            if (tabVal.contains("title") && tabVal["title"].is_string()) {
                tab.title = truncateString(tabVal["title"].get<std::string>());
            }

            // Tab type
            if (!tabVal.contains("type") || !tabVal["type"].is_string()) {
                if (outError) {
                    findTokenLocation(data, length, tabId, outError->line, outError->column);
                    outError->message = "Tab '" + tabId + "' missing required 'type'";
                }
                return false;
            }

            std::string typeStr = tabVal["type"].get<std::string>();
            if (typeStr == "list") {
                tab.type = MfdTabType::kList;
            } else if (typeStr == "keyvalue") {
                tab.type = MfdTabType::kKeyValue;
            } else if (typeStr == "text") {
                tab.type = MfdTabType::kText;
            } else if (typeStr == "bars") {
                tab.type = MfdTabType::kBars;
            } else {
                if (outError) {
                    findTokenLocation(data, length, typeStr, outError->line, outError->column);
                    outError->message = "Unknown tab type: '" + typeStr + "' in tab '" + tabId + "'";
                }
                return false;
            }

            // Parse tab items according to type
            if (tab.type == MfdTabType::kList) {
                if (tabVal.contains("items") && tabVal["items"].is_array()) {
                    const auto& itemsArr = tabVal["items"];
                    size_t itemCount = itemsArr.size();
                    if (itemCount > kMaxItemsPerTab) {
                        static bool s_warnedListOverflow = false;
                        if (!s_warnedListOverflow) {
                            s_warnedListOverflow = true;
                            Log::get().note("mfd_schema: List tab '%s' item count %zu exceeds %zu, truncating\n",
                                            tabId.c_str(), itemCount, kMaxItemsPerTab);
                        }
                        itemCount = kMaxItemsPerTab;
                    }

                    std::unordered_set<std::string> itemIds;

                    for (size_t i = 0; i < itemCount; ++i) {
                        const auto& itemVal = itemsArr[i];
                        if (!itemVal.is_object()) continue;

                        if (!itemVal.contains("id") || !itemVal["id"].is_string() || itemVal["id"].get<std::string>().empty()) {
                            if (outError) {
                                findTokenLocation(data, length, itemVal.dump(), outError->line, outError->column);
                                outError->message = "List item missing required 'id' in tab '" + tabId + "'";
                            }
                            return false;
                        }

                        std::string itemId = itemVal["id"].get<std::string>();
                        if (itemIds.find(itemId) != itemIds.end()) {
                            if (outError) {
                                findTokenLocation(data, length, itemId, outError->line, outError->column);
                                outError->message = "Duplicate item id: '" + itemId + "' in tab '" + tabId + "'";
                            }
                            return false;
                        }
                        itemIds.insert(itemId);

                        MfdListItem item;
                        item.id = itemId;
                        if (itemVal.contains("label") && itemVal["label"].is_string()) {
                            item.label = truncateString(itemVal["label"].get<std::string>());
                        }
                        if (itemVal.contains("sublabel") && itemVal["sublabel"].is_string()) {
                            item.sublabel = truncateString(itemVal["sublabel"].get<std::string>());
                        }
                        if (itemVal.contains("value") && itemVal["value"].is_string()) {
                            item.value = truncateString(itemVal["value"].get<std::string>());
                        }
                        if (itemVal.contains("badge") && itemVal["badge"].is_string()) {
                            item.badge = truncateString(itemVal["badge"].get<std::string>());
                        }
                        if (itemVal.contains("valueColor") && itemVal["valueColor"].is_string()) {
                            item.valueColorName = itemVal["valueColor"].get<std::string>();
                            item.valueNamedColor = parseNamedColor(item.valueColorName);
                            item.valueColor = resolveNamedColor(item.valueNamedColor, activeTheme);
                        } else {
                            item.valueColorName = "normal";
                            item.valueNamedColor = MfdNamedColor::kNormal;
                            item.valueColor = resolveNamedColor(MfdNamedColor::kNormal, activeTheme);
                        }
                        if (itemVal.contains("icon") && itemVal["icon"].is_string()) {
                            item.iconName = itemVal["icon"].get<std::string>();
                            item.icon = parseIcon(item.iconName);
                        }
                        if (itemVal.contains("action") && itemVal["action"].is_string()) {
                            item.action = parseItemAction(itemVal["action"].get<std::string>());
                        }
                        if (itemVal.contains("checks") && itemVal["checks"].is_array()) {
                            const auto& checksArr = itemVal["checks"];
                            size_t checkCount = checksArr.size();
                            if (checkCount > kMaxChecksPerItem) checkCount = kMaxChecksPerItem;
                            for (size_t c = 0; c < checkCount; ++c) {
                                const auto& checkVal = checksArr[c];
                                if (checkVal.is_object()) {
                                    MfdCheckItem chk;
                                    if (checkVal.contains("label") && checkVal["label"].is_string()) {
                                        chk.label = truncateString(checkVal["label"].get<std::string>(), 2);
                                    }
                                    if (checkVal.contains("state") && checkVal["state"].is_string()) {
                                        chk.state = parseCheckState(checkVal["state"].get<std::string>());
                                    }
                                    item.checks.push_back(chk);
                                }
                            }
                        }
                        tab.items.push_back(std::move(item));
                    }
                }
            } else if (tab.type == MfdTabType::kKeyValue) {
                if (tabVal.contains("items") && tabVal["items"].is_array()) {
                    const auto& itemsArr = tabVal["items"];
                    size_t itemCount = itemsArr.size();
                    if (itemCount > kMaxItemsPerTab) {
                        static bool s_warnedKvOverflow = false;
                        if (!s_warnedKvOverflow) {
                            s_warnedKvOverflow = true;
                            Log::get().note("mfd_schema: KeyValue tab '%s' item count %zu exceeds %zu, truncating\n",
                                            tabId.c_str(), itemCount, kMaxItemsPerTab);
                        }
                        itemCount = kMaxItemsPerTab;
                    }

                    std::unordered_set<std::string> itemIds;

                    for (size_t i = 0; i < itemCount; ++i) {
                        const auto& itemVal = itemsArr[i];
                        if (!itemVal.is_object()) continue;

                        if (!itemVal.contains("id") || !itemVal["id"].is_string() || itemVal["id"].get<std::string>().empty()) {
                            if (outError) {
                                findTokenLocation(data, length, itemVal.dump(), outError->line, outError->column);
                                outError->message = "KeyValue item missing required 'id' in tab '" + tabId + "'";
                            }
                            return false;
                        }

                        std::string itemId = itemVal["id"].get<std::string>();
                        if (itemIds.find(itemId) != itemIds.end()) {
                            if (outError) {
                                findTokenLocation(data, length, itemId, outError->line, outError->column);
                                outError->message = "Duplicate item id: '" + itemId + "' in tab '" + tabId + "'";
                            }
                            return false;
                        }
                        itemIds.insert(itemId);

                        MfdKeyValue kv;
                        kv.id = itemId;
                        if (itemVal.contains("key") && itemVal["key"].is_string()) {
                            kv.key = truncateString(itemVal["key"].get<std::string>());
                        }
                        if (itemVal.contains("value") && itemVal["value"].is_string()) {
                            kv.value = truncateString(itemVal["value"].get<std::string>());
                        }
                        if (itemVal.contains("valueColor") && itemVal["valueColor"].is_string()) {
                            kv.valueColorName = itemVal["valueColor"].get<std::string>();
                            kv.valueNamedColor = parseNamedColor(kv.valueColorName);
                            kv.valueColor = resolveNamedColor(kv.valueNamedColor, activeTheme);
                        } else {
                            kv.valueColorName = "normal";
                            kv.valueNamedColor = MfdNamedColor::kNormal;
                            kv.valueColor = resolveNamedColor(MfdNamedColor::kNormal, activeTheme);
                        }
                        if (itemVal.contains("action") && itemVal["action"].is_string()) {
                            kv.action = parseItemAction(itemVal["action"].get<std::string>());
                        }
                        tab.keyValues.push_back(std::move(kv));
                    }
                }
            } else if (tab.type == MfdTabType::kText) {
                if (tabVal.contains("text") && tabVal["text"].is_string()) {
                    std::string fullText = tabVal["text"].get<std::string>();
                    size_t start = 0;
                    while (start < fullText.size()) {
                        size_t nl = fullText.find('\n', start);
                        if (nl == std::string::npos) {
                            tab.lines.push_back(truncateString(fullText.substr(start)));
                            break;
                        }
                        tab.lines.push_back(truncateString(fullText.substr(start, nl - start)));
                        start = nl + 1;
                    }
                } else if (tabVal.contains("lines") && tabVal["lines"].is_array()) {
                    for (const auto& lineVal : tabVal["lines"]) {
                        if (lineVal.is_string()) {
                            tab.lines.push_back(truncateString(lineVal.get<std::string>()));
                        }
                    }
                }
            } else if (tab.type == MfdTabType::kBars) {
                if (tabVal.contains("items") && tabVal["items"].is_array()) {
                    const auto& itemsArr = tabVal["items"];
                    size_t itemCount = itemsArr.size();
                    if (itemCount > kMaxItemsPerTab) {
                        static bool s_warnedBarsOverflow = false;
                        if (!s_warnedBarsOverflow) {
                            s_warnedBarsOverflow = true;
                            Log::get().note("mfd_schema: Bars tab '%s' item count %zu exceeds %zu, truncating\n",
                                            tabId.c_str(), itemCount, kMaxItemsPerTab);
                        }
                        itemCount = kMaxItemsPerTab;
                    }

                    std::unordered_set<std::string> itemIds;

                    for (size_t i = 0; i < itemCount; ++i) {
                        const auto& itemVal = itemsArr[i];
                        if (!itemVal.is_object()) continue;

                        if (!itemVal.contains("id") || !itemVal["id"].is_string() || itemVal["id"].get<std::string>().empty()) {
                            if (outError) {
                                findTokenLocation(data, length, itemVal.dump(), outError->line, outError->column);
                                outError->message = "Bar item missing required 'id' in tab '" + tabId + "'";
                            }
                            return false;
                        }

                        std::string itemId = itemVal["id"].get<std::string>();
                        if (itemIds.find(itemId) != itemIds.end()) {
                            if (outError) {
                                findTokenLocation(data, length, itemId, outError->line, outError->column);
                                outError->message = "Duplicate item id: '" + itemId + "' in tab '" + tabId + "'";
                            }
                            return false;
                        }
                        itemIds.insert(itemId);

                        MfdBarItem bar;
                        bar.id = itemId;
                        if (itemVal.contains("label") && itemVal["label"].is_string()) {
                            bar.label = truncateString(itemVal["label"].get<std::string>());
                        }
                        if (itemVal.contains("value") && itemVal["value"].is_number()) {
                            bar.value = itemVal["value"].get<float>();
                        }
                        if (itemVal.contains("max") && itemVal["max"].is_number()) {
                            bar.max = itemVal["max"].get<float>();
                        } else {
                            bar.max = 1.0f;
                        }
                        if (itemVal.contains("segments") && itemVal["segments"].is_number_integer()) {
                            bar.segments = itemVal["segments"].get<int>();
                        }
                        if (itemVal.contains("color") && itemVal["color"].is_string()) {
                            bar.colorName = itemVal["color"].get<std::string>();
                            bar.namedColor = parseNamedColor(bar.colorName);
                            bar.resolvedColor = resolveNamedColor(bar.namedColor, activeTheme);
                        } else {
                            bar.colorName = "normal";
                            bar.namedColor = MfdNamedColor::kNormal;
                            bar.resolvedColor = resolveNamedColor(MfdNamedColor::kNormal, activeTheme);
                        }
                        tab.barItems.push_back(std::move(bar));
                    }
                }
            }

            vm.tabs.push_back(std::move(tab));
        }
    }

    outVm = std::move(vm);
    return true;
}

} // namespace edvr::mfd
