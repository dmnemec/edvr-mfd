#include "mfd_provider.h"
#include "mfd_display_schema.h"
#include <nlohmann/json.hpp>

namespace edvr::mfd {

bool DeclarativeMfdProvider::loadFromJson(const std::string& json) {
    MfdParseError error;
    if (parseDisplayJson(json, m_model, &error)) {
        return true;
    }

    // Lenient fallback for legacy / prototype JSON formats (e.g. ensureDefaultSlots before Ticket A6)
    try {
        auto root = nlohmann::json::parse(json);
        if (!root.is_object()) return false;

        if (root.contains("title") && root["title"].is_string()) {
            m_model.title = root["title"].get<std::string>();
        }
        if (root.contains("subtitle") && root["subtitle"].is_string()) {
            m_model.subtitle = root["subtitle"].get<std::string>();
        }
        if (root.contains("statusBadge") && root["statusBadge"].is_string()) {
            m_model.statusBadge = root["statusBadge"].get<std::string>();
        }
        if (root.contains("footerHint") && root["footerHint"].is_string()) {
            m_model.footerHint = root["footerHint"].get<std::string>();
        }

        if (root.contains("tabs") && root["tabs"].is_array()) {
            m_model.tabs.clear();
            int tIdx = 0;
            for (const auto& tabVal : root["tabs"]) {
                if (!tabVal.is_object()) continue;
                MfdTab tab;
                tab.id = tabVal.value("id", "tab_" + std::to_string(tIdx++));
                tab.title = tabVal.value("title", "TAB");
                std::string typeStr = tabVal.value("type", "list");
                if (typeStr == "keyvalue") {
                    tab.type = MfdTabType::kKeyValue;
                } else if (typeStr == "text") {
                    tab.type = MfdTabType::kText;
                } else if (typeStr == "bars") {
                    tab.type = MfdTabType::kBars;
                } else {
                    tab.type = MfdTabType::kList;
                }

                if (tabVal.contains("items") && tabVal["items"].is_array()) {
                    int iIdx = 0;
                    for (const auto& itemVal : tabVal["items"]) {
                        if (tab.type == MfdTabType::kList && itemVal.is_object()) {
                            MfdListItem item;
                            item.id = itemVal.value("id", "item_" + std::to_string(iIdx++));
                            item.label = itemVal.value("label", "");
                            item.value = itemVal.value("value", "");
                            item.sublabel = itemVal.value("sublabel", "");
                            item.badge = itemVal.value("badge", "");
                            tab.items.push_back(std::move(item));
                        } else if (tab.type == MfdTabType::kKeyValue && itemVal.is_object()) {
                            MfdKeyValue kv;
                            kv.key = itemVal.value("key", "");
                            kv.value = itemVal.value("value", "");
                            kv.id = itemVal.value("id", kv.key.empty() ? ("kv_" + std::to_string(iIdx++)) : kv.key);
                            tab.keyValues.push_back(std::move(kv));
                        } else if (tab.type == MfdTabType::kText && itemVal.is_string()) {
                            tab.lines.push_back(itemVal.get<std::string>());
                        }
                    }
                }
                m_model.tabs.push_back(std::move(tab));
            }
            return true;
        }
    } catch (...) {}

    return false;
}

} // namespace edvr::mfd
