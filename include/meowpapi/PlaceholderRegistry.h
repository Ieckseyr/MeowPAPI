// PlaceholderRegistry.h - PAPI 占位符注册表核心
//
// 提供：
// - 三类占位符注册（服务器级 / 玩家级 / 静态缓存）
// - 字符串翻译（%name% 和 {name} 两种格式）
// - 占位符注销与按插件批量注销
#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class Player;

namespace meowpapi {

// 占位符类型
enum class PlaceholderType : uint8_t {
    Server, // 服务器级，无参数
    Player, // 玩家级，带 Player*
    Static, // 静态缓存，带更新间隔
};

// 占位符回调（统一签名，player 可为 nullptr 表示服务器级）
using PlaceholderCallback = std::function<std::string(Player* player)>;

// 占位符条目
struct PlaceholderEntry {
    std::string                                              pluginName;
    std::string                                              name;
    PlaceholderType                                          type;
    PlaceholderCallback                                      callback;
    int                                                      updateIntervalMs = 0;
    mutable std::string                                      cachedValue;
    mutable std::chrono::steady_clock::time_point            lastUpdate;
    // 标记此占位符回调只能在主线程调用（如 RemoteCall JS 回调，LSE 引擎单线程）
    // 后台线程翻译时需跳过此类占位符，留给主线程第二遍翻译
    bool                                                     mainThreadOnly = false;
};

// 占位符注册表单例
// MeowSidebar 持有主注册表；其他插件通过 RemoteCall 访问。
class PlaceholderRegistry {
public:
    static PlaceholderRegistry& getInstance();

    // 注册占位符（返回 false 如果已存在同名占位符）
    bool registerServerPlaceholder(
        std::string const&   pluginName,
        std::string const&   name,
        PlaceholderCallback  cb
    );
    bool registerPlayerPlaceholder(
        std::string const&   pluginName,
        std::string const&   name,
        PlaceholderCallback  cb
    );
    bool registerStaticPlaceholder(
        std::string const&   pluginName,
        std::string const&   name,
        PlaceholderCallback  cb,
        int                  updateIntervalMs
    );

    // 带 mainThreadOnly 标记的注册版本（用于 RemoteCall JS 回调等只能在主线程调用的占位符）
    bool registerServerPlaceholderRemote(
        std::string const&   pluginName,
        std::string const&   name,
        PlaceholderCallback  cb
    );
    bool registerPlayerPlaceholderRemote(
        std::string const&   pluginName,
        std::string const&   name,
        PlaceholderCallback  cb
    );
    bool registerStaticPlaceholderRemote(
        std::string const&   pluginName,
        std::string const&   name,
        PlaceholderCallback  cb,
        int                  updateIntervalMs
    );

    // 注销
    bool                     unregisterPlaceholder(std::string const& name);
    void                     unregisterByPlugin(std::string const& pluginName);
    void                     clear();

    // 获取单个占位符值
    std::string              getValue(std::string const& name);
    std::string              getValueWithPlayer(std::string const& name, Player* player);

    // 翻译字符串：替换所有 %name% 和 {name} 格式的占位符
    std::string              translateString(std::string const& str);
    std::string              translateStringWithPlayer(std::string const& str, Player* player);
    // 带 skipRemote 参数的翻译：当 skipRemote=true 时跳过 mainThreadOnly=true 的占位符
    // （保留原样占位符文本，留给主线程第二遍翻译）
    std::string              translateStringWithPlayer(std::string const& str, Player* player, bool skipRemote);

    // 检查
    bool                     hasPlaceholder(std::string const& name);
    std::vector<std::string> listPlaceholders();
    std::vector<std::string> listPlaceholdersByPlugin(std::string const& pluginName);

    // 设置回退解析器（当本地注册表找不到占位符时调用）
    // 用于 BEPAPI 双向兼容：BepApiBridge 设置此解析器，回退到 BEPAPI 查询
    using FallbackResolver = std::function<std::string(std::string const& name, Player* player)>;
    void setFallbackResolver(FallbackResolver resolver);

private:
    PlaceholderRegistry() = default;

    std::unordered_map<std::string, PlaceholderEntry> mPlaceholders;
    FallbackResolver                                  mFallbackResolver;

    // 更新静态占位符缓存
    void updateStaticCache(PlaceholderEntry const& entry) const;

    // 解析单个占位符名称（不含定界符）
    // skipRemote=true 时跳过 mainThreadOnly 占位符（保留原始占位符文本）
    std::string resolvePlaceholder(std::string const& name, Player* player, bool skipRemote);
};

} // namespace meowpapi
