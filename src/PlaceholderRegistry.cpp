// PlaceholderRegistry.cpp - PAPI 占位符注册表实现
#include "meowpapi/PlaceholderRegistry.h"
#include "meowpapi/DllExports.h"

#include <algorithm>
#include <mutex>
#include <regex>

#include <nlohmann/json.hpp>

namespace meowpapi {

namespace {

//===== ${papi:...} 表达式解析辅助（GMLIB PAPI 兼容）=====

// 段是否为整段插槽形式 "<param>"（GMLIB 槽位模板语义）
bool isSlotSegment(std::string const& seg) {
    return seg.size() >= 3 && seg.front() == '<' && seg.back() == '>'
        && seg.find('<', 1) == std::string::npos
        && seg.find('>', 1) == std::string::npos;
}

// 提取插槽参数名（"<score>" → "score"），非插槽段返回空
std::string slotName(std::string const& seg) {
    if (!isSlotSegment(seg)) return {};
    return seg.substr(1, seg.size() - 2);
}

// 按 '_' 分割
std::vector<std::string> splitUnderscore(std::string const& str) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t pos = str.find('_', start);
        if (pos == std::string::npos) {
            parts.push_back(str.substr(start));
            break;
        }
        parts.push_back(str.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

// GMLIB 兼容槽位模板匹配：templateName 含 <param> 整段插槽，与 actualName
// 按 '_' 分段匹配（段数相等，插槽段捕获实际段为参数值，其余段字面相等）
bool matchSlotTemplate(
    std::string const&                        templateName,
    std::string const&                        actualName,
    std::unordered_map<std::string, std::string>& outParams
) {
    auto tp = splitUnderscore(templateName);
    auto ap = splitUnderscore(actualName);
    if (tp.size() != ap.size()) return false;
    std::unordered_map<std::string, std::string> params;
    for (size_t i = 0; i < tp.size(); ++i) {
        auto name = slotName(tp[i]);
        if (!name.empty()) {
            if (ap[i].empty()) return false;
            params[name] = ap[i];
        } else if (tp[i] != ap[i]) {
            return false;
        }
    }
    if (params.empty()) return false;
    outParams = std::move(params);
    return true;
}

// 键名标准化：去掉首尾尖括号（"<score>" → "score"，"score" → "score"）
std::string normalizeKey(std::string key) {
    if (key.size() >= 2 && key.front() == '<' && key.back() == '>') {
        key = key.substr(1, key.size() - 2);
    }
    return key;
}

// 在 str 中查找匹配的 '}'（从 start+1 开始，嵌套感知：遇 "${" 深度+1，遇 "}" 深度-1）
// 返回匹配的 '}' 位置，未找到返回 npos
size_t findMatchingBrace(std::string const& str, size_t start) {
    int    depth = 1;
    size_t i     = start;
    size_t len   = str.size();
    while (i < len) {
        if (str[i] == '\\' && i + 1 < len) {
            i += 2; // 跳过转义字符
            continue;
        }
        if (str[i] == '$' && i + 1 < len && str[i + 1] == '{') {
            ++depth;
            i += 2;
            continue;
        }
        if (str[i] == '}') {
            --depth;
            if (depth == 0) return i;
        }
        ++i;
    }
    return std::string::npos;
}

// 查找第一个顶层出现的分隔符集合中的字符（跳过转义与嵌套 "${...}"）
// 返回 npos 表示未找到
size_t findTopLevelAny(std::string const& str, size_t from, std::string const& seps) {
    size_t i   = from;
    size_t len = str.size();
    while (i < len) {
        if (str[i] == '\\' && i + 1 < len) {
            i += 2;
            continue;
        }
        if (str[i] == '$' && i + 1 < len && str[i + 1] == '{') {
            // 跳过嵌套表达式
            size_t close = findMatchingBrace(str, i + 1);
            if (close == std::string::npos) return std::string::npos;
            i = close + 1;
            continue;
        }
        if (seps.find(str[i]) != std::string::npos) return i;
        ++i;
    }
    return std::string::npos;
}

// 还原转义："\," → "," 等（保守处理：只还原常见分隔符与反斜杠）
std::string unescape(std::string const& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()
            && (s[i + 1] == ',' || s[i + 1] == ';' || s[i + 1] == '|' || s[i + 1] == ':'
                || s[i + 1] == '=' || s[i + 1] == '\\')) {
            out += s[i + 1];
            ++i;
        } else {
            out += s[i];
        }
    }
    return out;
}

} // anonymous namespace

PlaceholderRegistry& PlaceholderRegistry::getInstance() {
    static PlaceholderRegistry instance;
    return instance;
}

bool PlaceholderRegistry::registerServerPlaceholder(
    std::string const& pluginName, std::string const& name, PlaceholderCallback cb
) {
    if (name.empty() || !cb) return false;
    auto key = name;
    if (mPlaceholders.count(key)) return false;
    PlaceholderEntry entry{};
    entry.pluginName = pluginName;
    entry.name       = name;
    entry.type       = PlaceholderType::Server;
    entry.callback   = std::move(cb);
    mPlaceholders.emplace(std::move(key), std::move(entry));
    return true;
}

bool PlaceholderRegistry::registerPlayerPlaceholder(
    std::string const& pluginName, std::string const& name, PlaceholderCallback cb
) {
    if (name.empty() || !cb) return false;
    auto key = name;
    if (mPlaceholders.count(key)) return false;
    PlaceholderEntry entry{};
    entry.pluginName = pluginName;
    entry.name       = name;
    entry.type       = PlaceholderType::Player;
    entry.callback   = std::move(cb);
    mPlaceholders.emplace(std::move(key), std::move(entry));
    return true;
}

bool PlaceholderRegistry::registerStaticPlaceholder(
    std::string const& pluginName, std::string const& name, PlaceholderCallback cb, int updateIntervalMs
) {
    if (name.empty() || !cb) return false;
    auto key = name;
    if (mPlaceholders.count(key)) return false;
    PlaceholderEntry entry{};
    entry.pluginName       = pluginName;
    entry.name             = name;
    entry.type             = PlaceholderType::Static;
    entry.callback         = std::move(cb);
    entry.updateIntervalMs = updateIntervalMs > 0 ? updateIntervalMs : 1000;
    entry.lastUpdate       = std::chrono::steady_clock::time_point{};
    mPlaceholders.emplace(std::move(key), std::move(entry));
    return true;
}

// RemoteCall 版本：设置 mainThreadOnly=true，后台线程跳过这些占位符
bool PlaceholderRegistry::registerServerPlaceholderRemote(
    std::string const& pluginName, std::string const& name, PlaceholderCallback cb
) {
    if (name.empty() || !cb) return false;
    auto key = name;
    if (mPlaceholders.count(key)) return false;
    PlaceholderEntry entry{};
    entry.pluginName     = pluginName;
    entry.name           = name;
    entry.type           = PlaceholderType::Server;
    entry.callback       = std::move(cb);
    entry.mainThreadOnly = true;
    mPlaceholders.emplace(std::move(key), std::move(entry));
    return true;
}

bool PlaceholderRegistry::registerPlayerPlaceholderRemote(
    std::string const& pluginName, std::string const& name, PlaceholderCallback cb
) {
    if (name.empty() || !cb) return false;
    auto key = name;
    if (mPlaceholders.count(key)) return false;
    PlaceholderEntry entry{};
    entry.pluginName     = pluginName;
    entry.name           = name;
    entry.type           = PlaceholderType::Player;
    entry.callback       = std::move(cb);
    entry.mainThreadOnly = true;
    mPlaceholders.emplace(std::move(key), std::move(entry));
    return true;
}

bool PlaceholderRegistry::registerStaticPlaceholderRemote(
    std::string const& pluginName, std::string const& name, PlaceholderCallback cb, int updateIntervalMs
) {
    if (name.empty() || !cb) return false;
    auto key = name;
    if (mPlaceholders.count(key)) return false;
    PlaceholderEntry entry{};
    entry.pluginName       = pluginName;
    entry.name             = name;
    entry.type             = PlaceholderType::Static;
    entry.callback         = std::move(cb);
    entry.updateIntervalMs = updateIntervalMs > 0 ? updateIntervalMs : 1000;
    entry.lastUpdate       = std::chrono::steady_clock::time_point{};
    entry.mainThreadOnly   = true;
    mPlaceholders.emplace(std::move(key), std::move(entry));
    return true;
}

// 通过回调 ID 注册（用于 DLL 边界外的回调）
bool PlaceholderRegistry::registerPlaceholderWithCallbackId(
    std::string const& pluginName, std::string const& name,
    PlaceholderType type, uint64_t callbackId, int updateIntervalMs, bool mainThreadOnly
) {
    if (name.empty() || callbackId == 0) return false;
    auto key = name;
    if (mPlaceholders.count(key)) return false;
    PlaceholderEntry entry{};
    entry.pluginName       = pluginName;
    entry.name             = name;
    entry.type             = type;
    entry.callbackId       = callbackId;
    entry.mainThreadOnly   = mainThreadOnly;
    if (type == PlaceholderType::Static) {
        entry.updateIntervalMs = updateIntervalMs > 0 ? updateIntervalMs : 1000;
        entry.lastUpdate       = std::chrono::steady_clock::time_point{};
    }
    mPlaceholders.emplace(std::move(key), std::move(entry));
    return true;
}

// 带参数注册（GMLIB PAPI 兼容）：占位符名可含 <param> 插槽模板
bool PlaceholderRegistry::registerServerPlaceholderWithParams(
    std::string const& pluginName, std::string const& name, PlaceholderParamCallback cb
) {
    if (name.empty() || !cb) return false;
    auto key = name;
    if (mPlaceholders.count(key)) return false;
    PlaceholderEntry entry{};
    entry.pluginName    = pluginName;
    entry.name          = name;
    entry.type          = PlaceholderType::Server;
    entry.paramCallback = std::move(cb);
    entry.hasParams     = true;
    mPlaceholders.emplace(std::move(key), std::move(entry));
    return true;
}

bool PlaceholderRegistry::registerPlayerPlaceholderWithParams(
    std::string const& pluginName, std::string const& name, PlaceholderParamCallback cb
) {
    if (name.empty() || !cb) return false;
    auto key = name;
    if (mPlaceholders.count(key)) return false;
    PlaceholderEntry entry{};
    entry.pluginName    = pluginName;
    entry.name          = name;
    entry.type          = PlaceholderType::Player;
    entry.paramCallback = std::move(cb);
    entry.hasParams     = true;
    mPlaceholders.emplace(std::move(key), std::move(entry));
    return true;
}

bool PlaceholderRegistry::registerServerPlaceholderWithParamsRemote(
    std::string const& pluginName, std::string const& name, PlaceholderParamCallback cb
) {
    if (name.empty() || !cb) return false;
    auto key = name;
    if (mPlaceholders.count(key)) return false;
    PlaceholderEntry entry{};
    entry.pluginName     = pluginName;
    entry.name           = name;
    entry.type           = PlaceholderType::Server;
    entry.paramCallback  = std::move(cb);
    entry.hasParams      = true;
    entry.mainThreadOnly = true;
    mPlaceholders.emplace(std::move(key), std::move(entry));
    return true;
}

bool PlaceholderRegistry::registerPlayerPlaceholderWithParamsRemote(
    std::string const& pluginName, std::string const& name, PlaceholderParamCallback cb
) {
    if (name.empty() || !cb) return false;
    auto key = name;
    if (mPlaceholders.count(key)) return false;
    PlaceholderEntry entry{};
    entry.pluginName     = pluginName;
    entry.name           = name;
    entry.type           = PlaceholderType::Player;
    entry.paramCallback  = std::move(cb);
    entry.hasParams      = true;
    entry.mainThreadOnly = true;
    mPlaceholders.emplace(std::move(key), std::move(entry));
    return true;
}

bool PlaceholderRegistry::registerPlaceholderWithParamsAndCallbackId(
    std::string const& pluginName, std::string const& name,
    PlaceholderType type, uint64_t callbackId, int updateIntervalMs, bool mainThreadOnly
) {
    if (name.empty() || callbackId == 0) return false;
    auto key = name;
    if (mPlaceholders.count(key)) return false;
    PlaceholderEntry entry{};
    entry.pluginName       = pluginName;
    entry.name             = name;
    entry.type             = type;
    entry.callbackId       = callbackId;
    entry.hasParams        = true;
    entry.mainThreadOnly   = mainThreadOnly;
    if (type == PlaceholderType::Static) {
        entry.updateIntervalMs = updateIntervalMs > 0 ? updateIntervalMs : 1000;
        entry.lastUpdate       = std::chrono::steady_clock::time_point{};
    }
    mPlaceholders.emplace(std::move(key), std::move(entry));
    return true;
}

bool PlaceholderRegistry::unregisterPlaceholder(std::string const& name) {
    return mPlaceholders.erase(name) > 0;
}

void PlaceholderRegistry::unregisterByPlugin(std::string const& pluginName) {
    for (auto it = mPlaceholders.begin(); it != mPlaceholders.end();) {
        if (it->second.pluginName == pluginName) {
            it = mPlaceholders.erase(it);
        } else {
            ++it;
        }
    }
}

void PlaceholderRegistry::clear() { mPlaceholders.clear(); }

void PlaceholderRegistry::updateStaticCache(PlaceholderEntry const& entry) const {
    auto now = std::chrono::steady_clock::now();
    if (entry.lastUpdate == std::chrono::steady_clock::time_point{}) {
        // 首次访问
        entry.cachedValue = invokeEntryCallback(entry, nullptr);
        entry.lastUpdate  = now;
        return;
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - entry.lastUpdate);
    if (elapsed.count() >= entry.updateIntervalMs) {
        entry.cachedValue = invokeEntryCallback(entry, nullptr);
        entry.lastUpdate  = now;
    }
}

void PlaceholderRegistry::setFallbackResolver(FallbackResolver resolver) {
    mFallbackResolver = std::move(resolver);
}

void PlaceholderRegistry::setCallbackInvoker(CallbackInvoker invoker) {
    mCallbackInvoker = invoker;
}

void PlaceholderRegistry::setCallbackInvokerWithParams(CallbackInvokerWithParams invoker) {
    mCallbackInvokerWithParams = invoker;
}

std::string PlaceholderRegistry::invokeEntryCallback(PlaceholderEntry const& entry, Player* player) const {
    // 外部回调（callbackId 非零）：通过回调调用器调用静态库侧的 std::function
    if (entry.callbackId != 0) {
        if (!mCallbackInvoker) return "{ERR}";
        char buf[MEOWPAPI_MAX_RESULT];
        buf[0] = '\0';
        mCallbackInvoker(entry.callbackId, reinterpret_cast<void*>(player), buf, MEOWPAPI_MAX_RESULT);
        return std::string(buf);
    }
    // 内部回调：直接调用 std::function
    return entry.callback(player);
}

std::string PlaceholderRegistry::invokeEntryCallbackWithParams(
    PlaceholderEntry const& entry, Player* player, std::string const& paramsJson
) const {
    // 外部回调（callbackId 非零）：带参占位符走带参 invoker
    if (entry.callbackId != 0) {
        if (entry.hasParams) {
            if (!mCallbackInvokerWithParams) return "{ERR}";
            char buf[MEOWPAPI_MAX_RESULT];
            buf[0] = '\0';
            mCallbackInvokerWithParams(
                entry.callbackId, reinterpret_cast<void*>(player), paramsJson.c_str(), buf, MEOWPAPI_MAX_RESULT
            );
            return std::string(buf);
        }
        // 普通外部回调：忽略参数走旧 invoker（向后兼容）
        return invokeEntryCallback(entry, player);
    }
    // 内部带参回调优先
    if (entry.paramCallback) return entry.paramCallback(player, paramsJson);
    // 退化为普通回调（忽略参数）
    return entry.callback(player);
}

std::string PlaceholderRegistry::resolvePlaceholder(std::string const& name, Player* player, bool skipRemote) {
    std::string result;
    if (resolvePlaceholderInternal(name, player, skipRemote, nullptr, result)) {
        return result;
    }
    // 未命中：返回 {name} 形式，让上层第二遍翻译能再次匹配
    return std::string{"{"} + name + "}";
}

// 解析核心：精确匹配 → 槽位模板匹配（GMLIB <param> 兼容）
bool PlaceholderRegistry::resolvePlaceholderInternal(
    std::string const& name, Player* player, bool skipRemote,
    std::string const* paramsJson, std::string& outResult
) {
    // 1. 精确匹配
    auto it = mPlaceholders.find(name);
    if (it != mPlaceholders.end()) {
        auto const& entry = it->second;
        // 后台线程跳过 mainThreadOnly 占位符
        if (skipRemote && entry.mainThreadOnly) return false;
        try {
            switch (entry.type) {
                case PlaceholderType::Server:
                    // 无显式参数但 entry 为带参占位符时（%模板名% / {模板名} 直接引用），
                    // 传空参数对象，避免走 entry.callback 空函数路径
                    if (paramsJson) {
                        outResult = invokeEntryCallbackWithParams(entry, nullptr, *paramsJson);
                    } else if (entry.hasParams || entry.paramCallback) {
                        outResult = invokeEntryCallbackWithParams(entry, nullptr, "{}");
                    } else {
                        outResult = invokeEntryCallback(entry, nullptr);
                    }
                    return true;
                case PlaceholderType::Player:
                    if (paramsJson) {
                        outResult = invokeEntryCallbackWithParams(entry, player, *paramsJson);
                    } else if (entry.hasParams || entry.paramCallback) {
                        outResult = invokeEntryCallbackWithParams(entry, player, "{}");
                    } else {
                        outResult = invokeEntryCallback(entry, player);
                    }
                    return true;
                case PlaceholderType::Static:
                    updateStaticCache(entry);
                    outResult = entry.cachedValue;
                    return true;
            }
        } catch (...) {
            outResult = "{ERR}";
            return true;
        }
        outResult = "{ERR}";
        return true;
    }

    // 2. 槽位模板匹配（GMLIB <param> 兼容）：遍历含 '<' 的已注册模板
    //    仅当占位符名不含 '<' 时才有意义（含 '<' 说明是模板本身，精确已试过）
    if (name.find('<') == std::string::npos) {
        std::unordered_map<std::string, std::string> slotParams;
        for (auto const& [tplName, entry] : mPlaceholders) {
            if (tplName.find('<') == std::string::npos) continue;
            if (!matchSlotTemplate(tplName, name, slotParams)) continue;
            if (skipRemote && entry.mainThreadOnly) return false;
            // 槽位提取的参数与显式参数合并（显式优先）。
            // 键双格式注入：剥尖括号键（"score"）与带尖括号键（"<score>"，
            // GMLIB 原版语义，JS 回调 params['<score>'] 取值）同时写入
            nlohmann::json j = nlohmann::json::object();
            for (auto const& [k, v] : slotParams) {
                j[k]             = v;
                j["<" + k + ">"] = v;
            }
            if (paramsJson && !paramsJson->empty()) {
                try {
                    auto explicit_ = nlohmann::json::parse(*paramsJson);
                    if (explicit_.is_object()) {
                        for (auto it2 = explicit_.begin(); it2 != explicit_.end(); ++it2) {
                            j[it2.key()] = it2.value();
                        }
                    }
                } catch (...) {}
            }
            try {
                std::string merged = j.dump();
                switch (entry.type) {
                    case PlaceholderType::Server:
                        outResult = invokeEntryCallbackWithParams(entry, nullptr, merged);
                        return true;
                    case PlaceholderType::Player:
                        outResult = invokeEntryCallbackWithParams(entry, player, merged);
                        return true;
                    case PlaceholderType::Static:
                        updateStaticCache(entry);
                        outResult = entry.cachedValue;
                        return true;
                }
            } catch (...) {
                outResult = "{ERR}";
                return true;
            }
        }
    }

    // 3. 回退解析器（如 BEPAPI）
    //    注意：回退解析器也可能调用 RemoteCall，所以 skipRemote 时跳过
    if (!skipRemote && mFallbackResolver) {
        try {
            outResult = mFallbackResolver(name, player);
            return true;
        } catch (...) {
            outResult = "{ERR}";
            return true;
        }
    }
    return false;
}

// 翻译 ${papi:...} 表达式内部内容（不含 "${papi:" 前缀和 "}" 后缀）
// 格式：NAME 或 NAME,K1=V1,K2=V2,...
//   - NAME 可为槽位模板名（含 <param>）或实际名，可含嵌套 ${...}（先递归翻译）
//   - 参数键可带 <>（"<score>" 等价 "score"）；值可含嵌套 ${...}（递归翻译）与 \, \= 转义
// 参数合并为 JSON 对象字符串传给回调；槽位提取参数与显式参数在解析核心中合并（显式优先）
bool PlaceholderRegistry::translatePapiExpression(
    std::string const& inner, Player* player, bool skipRemote, std::string& outResult
) {
    // 顶层第一个 ',' 分离变量名与参数串
    size_t      comma      = findTopLevelAny(inner, 0, ",");
    size_t      nameLen    = (comma == std::string::npos) ? inner.size() : comma;
    std::string namePart   = inner.substr(0, nameLen);
    std::string paramsPart = (comma == std::string::npos) ? std::string{} : inner.substr(comma + 1);

    // 嵌套 ${...} 递归翻译：papi: 前缀递归表达式；否则简单占位符查找；未命中保留原文
    auto translateNested = [&](std::string const& s) -> std::string {
        std::string out;
        out.reserve(s.size());
        size_t i = 0;
        while (i < s.size()) {
            if (s[i] == '$' && i + 1 < s.size() && s[i + 1] == '{') {
                size_t close = findMatchingBrace(s, i + 1);
                if (close == std::string::npos) {
                    out += s.substr(i);
                    break;
                }
                std::string nested = s.substr(i + 2, close - i - 2);
                std::string res;
                bool        handled = false;
                if (nested.rfind("papi:", 0) == 0) {
                    handled = translatePapiExpression(nested.substr(5), player, skipRemote, res);
                } else {
                    handled = resolvePlaceholderInternal(nested, player, skipRemote, nullptr, res);
                }
                out += handled ? res : s.substr(i, close - i + 1);
                i = close + 1;
                continue;
            }
            out += s[i];
            ++i;
        }
        return out;
    };

    std::string name = unescape(translateNested(namePart));
    if (name.empty()) return false;

    // 解析参数串：顶层 ',' 分段，段内第一个顶层 '=' 分 key/value
    // 键双格式注入：原始键（"<number>"，GMLIB 原版语义，JS 回调
    // params['<number>'] 取值）与 normalizeKey 剥尖括号键（"number"）
    // 同时写入——两种写法均兼容
    nlohmann::json params = nlohmann::json::object();
    size_t         pos    = 0;
    while (pos < paramsPart.size()) {
        size_t      next   = findTopLevelAny(paramsPart, pos, ",");
        size_t      segEnd = (next == std::string::npos) ? paramsPart.size() : next;
        std::string seg    = paramsPart.substr(pos, segEnd - pos);
        pos                = (next == std::string::npos) ? paramsPart.size() : next + 1;
        if (seg.empty()) continue;
        size_t eq = findTopLevelAny(seg, 0, "=");
        if (eq == std::string::npos) continue;
        std::string rawKey = unescape(seg.substr(0, eq));
        std::string key    = normalizeKey(rawKey);
        std::string val    = unescape(translateNested(seg.substr(eq + 1)));
        if (key.empty()) continue;
        params[key]    = val;
        if (rawKey != key) params[rawKey] = val;
    }

    std::string paramsJson = params.dump();
    std::string result;
    if (resolvePlaceholderInternal(name, player, skipRemote, &paramsJson, result)) {
        outResult = std::move(result);
        return true;
    }
    return false;
}

std::string PlaceholderRegistry::getValue(std::string const& name) {
    return resolvePlaceholder(name, nullptr, false);
}

std::string PlaceholderRegistry::getValueWithPlayer(std::string const& name, Player* player) {
    return resolvePlaceholder(name, player, false);
}

// 翻译字符串：替换所有 %name% 和 {name} 格式的占位符
// 支持嵌套无关的两种格式：
//   %placeholder_name%  - BEPAPI 经典格式
//   {placeholder.name}  - BetterSidebar 格式
// 注意：{} 格式中如果包含冒号（如 {js:expr} 或 {E:name}），跳过不处理
std::string PlaceholderRegistry::translateString(std::string const& str) {
    return translateStringWithPlayer(str, nullptr, false);
}

std::string PlaceholderRegistry::translateStringWithPlayer(std::string const& str, Player* player) {
    return translateStringWithPlayer(str, player, false);
}

std::string PlaceholderRegistry::translateStringWithPlayer(std::string const& str, Player* player, bool skipRemote) {
    if (str.empty()) return str;

    std::string result;
    result.reserve(str.size() * 2);

    size_t i = 0;
    size_t len = str.size();
    while (i < len) {
        char c = str[i];
        // ${papi:...} 表达式（GMLIB PAPI 兼容，嵌套感知配对括号）
        if (c == '$' && i + 1 < len && str[i + 1] == '{') {
            size_t close = findMatchingBrace(str, i + 1);
            if (close != std::string::npos) {
                std::string expr = str.substr(i + 2, close - i - 2);
                if (expr.rfind("papi:", 0) == 0) {
                    std::string res;
                    if (translatePapiExpression(expr.substr(5), player, skipRemote, res)) {
                        result += res;
                    } else {
                        // 未命中或被 skipRemote 跳过：保留原文，留给主线程二遍翻译
                        result += str.substr(i, close - i + 1);
                    }
                    i = close + 1;
                    continue;
                }
                // 非 papi 前缀的 ${...}：不拦截，保持旧行为
                //（$ 按普通字符输出，后续 { 走原 {name} 逻辑）
            }
        }
        if (c == '%') {
            // 查找匹配的 %
            size_t end = str.find('%', i + 1);
            if (end != std::string::npos && end > i + 1) {
                std::string name = str.substr(i + 1, end - i - 1);
                // 只处理纯占位符（不含空格和特殊字符）
                if (name.find_first_of(" \t\n\r{}%") == std::string::npos) {
                    std::string resolved = resolvePlaceholder(name, player, skipRemote);
                    // 若返回的是 %name% 形式（不可能，因为 resolvePlaceholder 返回 {name}），
                    // 但跳过 mainThreadOnly 时返回的是 {name} 形式
                    // 此时需要保留为 %name% 形式以便第二遍翻译能匹配
                    if (skipRemote && resolved == std::string{"{"} + name + "}") {
                        // 保留 %name% 原始格式
                        result += str.substr(i, end - i + 1);
                        i = end + 1;
                        continue;
                    }
                    result += resolved;
                    i       = end + 1;
                    continue;
                }
            }
            result += c;
            ++i;
        } else if (c == '{') {
            // 查找匹配的 }
            size_t end = str.find('}', i + 1);
            if (end != std::string::npos && end > i + 1) {
                std::string inner = str.substr(i + 1, end - i - 1);
                // 跳过含冒号的表达式（如 {js:...}, {E:...}）
                // 跳过含空格的
                if (inner.find(':') != std::string::npos || inner.find(' ') != std::string::npos) {
                    result += c;
                    ++i;
                    continue;
                }
                // 这是一个 {name} 格式的占位符
                std::string resolved = resolvePlaceholder(inner, player, skipRemote);
                // 若 skipRemote 跳过了 mainThreadOnly 占位符，resolved 会等于 {inner}
                // 此时直接保留原始 {inner} 文本，让第二遍翻译能再次匹配
                if (skipRemote && resolved == std::string{"{"} + inner + "}") {
                    result += str.substr(i, end - i + 1);
                    i = end + 1;
                    continue;
                }
                result += resolved;
                i       = end + 1;
                continue;
            }
            result += c;
            ++i;
        } else {
            result += c;
            ++i;
        }
    }
    return result;
}

bool PlaceholderRegistry::hasPlaceholder(std::string const& name) {
    return mPlaceholders.count(name) > 0;
}

std::vector<std::string> PlaceholderRegistry::listPlaceholders() {
    std::vector<std::string> result;
    result.reserve(mPlaceholders.size());
    for (auto const& [name, entry] : mPlaceholders) {
        result.push_back(name);
    }
    return result;
}

std::vector<std::string> PlaceholderRegistry::listPlaceholdersByPlugin(std::string const& pluginName) {
    std::vector<std::string> result;
    for (auto const& [name, entry] : mPlaceholders) {
        if (entry.pluginName == pluginName) result.push_back(name);
    }
    return result;
}

std::vector<PlaceholderInfo> PlaceholderRegistry::listPlaceholderInfos() {
    std::vector<PlaceholderInfo> result;
    result.reserve(mPlaceholders.size());
    for (auto const& [name, entry] : mPlaceholders) {
        PlaceholderInfo info;
        info.name       = name;
        info.pluginName = entry.pluginName;
        info.type       = entry.type;
        info.hasParams  = entry.hasParams;
        result.push_back(std::move(info));
    }
    // 按名字排序，保证分页浏览顺序稳定
    std::sort(result.begin(), result.end(),
        [](PlaceholderInfo const& a, PlaceholderInfo const& b) { return a.name < b.name; });
    return result;
}

} // namespace meowpapi
