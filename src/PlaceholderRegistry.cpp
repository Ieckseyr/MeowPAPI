// PlaceholderRegistry.cpp - PAPI 占位符注册表实现
#include "meowpapi/PlaceholderRegistry.h"

#include <algorithm>
#include <mutex>
#include <regex>

namespace meowpapi {

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
        entry.cachedValue = entry.callback(nullptr);
        entry.lastUpdate  = now;
        return;
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - entry.lastUpdate);
    if (elapsed.count() >= entry.updateIntervalMs) {
        entry.cachedValue = entry.callback(nullptr);
        entry.lastUpdate  = now;
    }
}

void PlaceholderRegistry::setFallbackResolver(FallbackResolver resolver) {
    mFallbackResolver = std::move(resolver);
}

std::string PlaceholderRegistry::resolvePlaceholder(std::string const& name, Player* player, bool skipRemote) {
    auto it = mPlaceholders.find(name);
    if (it == mPlaceholders.end()) {
        // 本地未找到占位符，尝试回退解析器（如 BEPAPI）
        // 注意：回退解析器也可能调用 RemoteCall，所以 skipRemote 时也跳过
        if (!skipRemote && mFallbackResolver) {
            try {
                return mFallbackResolver(name, player);
            } catch (...) {
                return "{ERR}";
            }
        }
        // 无回退解析器或被跳过，返回原始占位符格式
        // 注意：返回 {name} 形式，让上层第二遍翻译能再次匹配
        return std::string{"{"} + name + "}";
    }
    auto const& entry = it->second;
    // 后台线程跳过 mainThreadOnly 占位符，保留原始格式 {name}
    if (skipRemote && entry.mainThreadOnly) {
        return std::string{"{"} + name + "}";
    }
    try {
        switch (entry.type) {
            case PlaceholderType::Server:
                return entry.callback(nullptr);
            case PlaceholderType::Player:
                return entry.callback(player);
            case PlaceholderType::Static:
                updateStaticCache(entry);
                return entry.cachedValue;
        }
    } catch (...) {
        return "{ERR}";
    }
    return "{ERR}";
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

} // namespace meowpapi
