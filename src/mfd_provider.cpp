#include "mfd_provider.h"
#include <cctype>
#include <map>
#include <utility>

namespace edvr::mfd {

namespace {

// Minimalist zero-dependency JSON token / value parser for declarative MFD state.
enum class JsonType { kNull, kBool, kNumber, kString, kArray, kObject };

struct JsonVal {
    JsonType type = JsonType::kNull;
    bool boolVal = false;
    double numVal = 0.0;
    std::string strVal;
    std::vector<JsonVal> arrVal;
    std::map<std::string, JsonVal> objVal;

    const JsonVal* get(const std::string& key) const {
        if (type != JsonType::kObject) return nullptr;
        auto it = objVal.find(key);
        return it != objVal.end() ? &it->second : nullptr;
    }

    std::string findString(const std::string& key, const std::string& def = "") const {
        const auto* v = get(key);
        return (v && v->type == JsonType::kString) ? v->strVal : def;
    }

    int findInt(const std::string& key, int def = 0) const {
        const auto* v = get(key);
        return (v && v->type == JsonType::kNumber) ? static_cast<int>(v->numVal) : def;
    }
};

class SimpleJsonParser {
public:
    explicit SimpleJsonParser(const std::string& src) : m_src(src) {}

    bool parse(JsonVal& out) {
        skipWs();
        if (m_pos >= m_src.size()) return false;
        return parseVal(out);
    }

private:
    const std::string& m_src;
    size_t m_pos = 0;

    void skipWs() {
        while (m_pos < m_src.size() && std::isspace(static_cast<unsigned char>(m_src[m_pos]))) {
            m_pos++;
        }
    }

    char peek() const {
        return m_pos < m_src.size() ? m_src[m_pos] : '\0';
    }

    char get() {
        return m_pos < m_src.size() ? m_src[m_pos++] : '\0';
    }

    bool parseVal(JsonVal& v) {
        skipWs();
        char c = peek();
        if (c == '{') return parseObj(v);
        if (c == '[') return parseArr(v);
        if (c == '"') return parseStr(v);
        if (c == 't' || c == 'f') return parseBool(v);
        if (c == 'n') return parseNull(v);
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNum(v);
        return false;
    }

    bool parseStr(JsonVal& v) {
        if (get() != '"') return false;
        v.type = JsonType::kString;
        v.strVal.clear();
        while (m_pos < m_src.size()) {
            char c = get();
            if (c == '"') return true;
            if (c == '\\' && m_pos < m_src.size()) {
                char esc = get();
                if (esc == '"') v.strVal.push_back('"');
                else if (esc == '\\') v.strVal.push_back('\\');
                else if (esc == '/') v.strVal.push_back('/');
                else if (esc == 'b') v.strVal.push_back('\b');
                else if (esc == 'f') v.strVal.push_back('\f');
                else if (esc == 'n') v.strVal.push_back('\n');
                else if (esc == 'r') v.strVal.push_back('\r');
                else if (esc == 't') v.strVal.push_back('\t');
                else v.strVal.push_back(esc);
            } else {
                v.strVal.push_back(c);
            }
        }
        return false;
    }

    bool parseNum(JsonVal& v) {
        size_t start = m_pos;
        if (peek() == '-') get();
        while (m_pos < m_src.size() && (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '.')) {
            get();
        }
        try {
            v.type = JsonType::kNumber;
            v.numVal = std::stod(m_src.substr(start, m_pos - start));
            return true;
        } catch (...) {
            return false;
        }
    }

    bool parseBool(JsonVal& v) {
        v.type = JsonType::kBool;
        if (m_src.compare(m_pos, 4, "true") == 0) {
            m_pos += 4;
            v.boolVal = true;
            return true;
        }
        if (m_src.compare(m_pos, 5, "false") == 0) {
            m_pos += 5;
            v.boolVal = false;
            return true;
        }
        return false;
    }

    bool parseNull(JsonVal& v) {
        v.type = JsonType::kNull;
        if (m_src.compare(m_pos, 4, "null") == 0) {
            m_pos += 4;
            return true;
        }
        return false;
    }

    bool parseArr(JsonVal& v) {
        if (get() != '[') return false;
        v.type = JsonType::kArray;
        v.arrVal.clear();
        skipWs();
        if (peek() == ']') { get(); return true; }

        while (true) {
            JsonVal item;
            if (!parseVal(item)) return false;
            v.arrVal.push_back(std::move(item));
            skipWs();
            char c = get();
            if (c == ']') return true;
            if (c != ',') return false;
            skipWs();
        }
    }

    bool parseObj(JsonVal& v) {
        if (get() != '{') return false;
        v.type = JsonType::kObject;
        v.objVal.clear();
        skipWs();
        if (peek() == '}') { get(); return true; }

        while (true) {
            skipWs();
            JsonVal keyVal;
            if (!parseStr(keyVal)) return false;
            skipWs();
            if (get() != ':') return false;
            JsonVal val;
            if (!parseVal(val)) return false;
            v.objVal[keyVal.strVal] = std::move(val);
            skipWs();
            char c = get();
            if (c == '}') return true;
            if (c != ',') return false;
        }
    }
};

} // namespace

bool DeclarativeMfdProvider::loadFromJson(const std::string& json) {
    SimpleJsonParser parser(json);
    JsonVal root;
    if (!parser.parse(root) || root.type != JsonType::kObject) {
        return false;
    }

    // Header values
    if (const auto* t = root.get("title")) {
        if (t->type == JsonType::kString) m_model.title = t->strVal;
    }
    if (const auto* s = root.get("subtitle")) {
        if (s->type == JsonType::kString) m_model.subtitle = s->strVal;
    }
    if (const auto* b = root.get("statusBadge")) {
        if (b->type == JsonType::kString) m_model.statusBadge = b->strVal;
    }
    if (const auto* h = root.get("footerHint")) {
        if (h->type == JsonType::kString) m_model.footerHint = h->strVal;
    }

    // Tabs array
    const auto* tabsVal = root.get("tabs");
    if (tabsVal && tabsVal->type == JsonType::kArray) {
        m_model.tabs.clear();
        for (const auto& tabJson : tabsVal->arrVal) {
            if (tabJson.type != JsonType::kObject) continue;

            MfdTab tab;
            tab.title = tabJson.findString("title", "TAB");
            std::string typeStr = tabJson.findString("type", "list");
            if (typeStr == "keyvalue") {
                tab.type = MfdTabType::kKeyValue;
            } else if (typeStr == "text") {
                tab.type = MfdTabType::kText;
            } else {
                tab.type = MfdTabType::kList;
            }

            // Items / content
            const auto* itemsVal = tabJson.get("items");
            if (itemsVal && itemsVal->type == JsonType::kArray) {
                for (const auto& itemJson : itemsVal->arrVal) {
                    if (tab.type == MfdTabType::kList && itemJson.type == JsonType::kObject) {
                        MfdListItem item;
                        item.id = itemJson.findString("id");
                        item.label = itemJson.findString("label");
                        item.value = itemJson.findString("value");
                        item.sublabel = itemJson.findString("sublabel");
                        item.badge = itemJson.findString("badge");
                        tab.items.push_back(std::move(item));
                    } else if (tab.type == MfdTabType::kKeyValue && itemJson.type == JsonType::kObject) {
                        MfdKeyValue kv;
                        kv.key = itemJson.findString("key");
                        kv.value = itemJson.findString("value");
                        tab.keyValues.push_back(std::move(kv));
                    } else if (tab.type == MfdTabType::kText && itemJson.type == JsonType::kString) {
                        tab.lines.push_back(itemJson.strVal);
                    }
                }
            }
            m_model.tabs.push_back(std::move(tab));
        }
    }

    return true;
}

} // namespace edvr::mfd
