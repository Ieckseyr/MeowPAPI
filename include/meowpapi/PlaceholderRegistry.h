// PlaceholderRegistry.h - PAPI 占位符注册表核心
//
// 提供：
// - 三类占位符注册（服务器级 / 玩家级 / 静态缓存）
// - 字符串翻译（%name% 和 {name} 两种格式）
// - 占位符注销与按插件批量注销
#pragma once

#include <chrono>
#include <cstdint>
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

// 带参数回调（GMLIB PAPI 兼容）：paramsJson 为参数键值对的 JSON 对象字符串
// 例如 ${papi:title_score_rank_<score>_<title>_<number>,<score>=zxsc,<title>=在线榜,<number>=10}
// 回调收到 {"score":"zxsc","title":"在线榜","number":"10"}
// 带参回调注册的占位符名可含 <param> 插槽模板（按 _ 分段匹配）
using PlaceholderParamCallback = std::function<std::string(Player* player, std::string const& paramsJson)>;

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
    // 外部回调 ID（非零时使用回调调用器代替 std::function）
    // 用于 DLL 边界外的回调：静态库存储 std::function，DLL 通过 ID 回调
    uint64_t                                                 callbackId = 0;
    // 带参数回调（非空时优先于 callback）
    PlaceholderParamCallback                                 paramCallback;
    // 标记此占位符为带参占位符（callbackId 路径下决定使用带参 invoker）
    bool                                                     hasParams = false;
};

// 占位符概要信息（/meowpapi list 指令与诊断用，不含回调）
struct PlaceholderInfo {
    std::string     name;
    std::string     pluginName;
    PlaceholderType type;
    bool            hasParams = false; // 带参占位符（GMLIB <param> 模板）
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

    // 通过回调 ID 注册（用于 DLL 边界外的回调）
    // 静态库侧存储 std::function，DLL 通过 callbackId 回调
    bool registerPlaceholderWithCallbackId(
        std::string const&   pluginName,
        std::string const&   name,
        PlaceholderType      type,
        uint64_t             callbackId,
        int                  updateIntervalMs,
        bool                 mainThreadOnly
    );

    // 带参数注册（GMLIB PAPI 兼容，add-only 扩展）
    // name 可含 <param> 插槽模板（如 "title_score_rank_<score>_<title>_<number>"），
    // 翻译时按 _ 分段匹配实际占位符名并提取参数；回调通过 paramsJson 收到参数
    bool registerServerPlaceholderWithParams(
        std::string const&        pluginName,
        std::string const&        name,
        PlaceholderParamCallback  cb
    );
    bool registerPlayerPlaceholderWithParams(
        std::string const&        pluginName,
        std::string const&        name,
        PlaceholderParamCallback  cb
    );
    // 带 mainThreadOnly 标记版本（RemoteCall JS 回调用）
    bool registerServerPlaceholderWithParamsRemote(
        std::string const&        pluginName,
        std::string const&        name,
        PlaceholderParamCallback  cb
    );
    bool registerPlayerPlaceholderWithParamsRemote(
        std::string const&        pluginName,
        std::string const&        name,
        PlaceholderParamCallback  cb
    );
    // 通过回调 ID 注册带参占位符（DLL 边界外的带参回调）
    bool registerPlaceholderWithParamsAndCallbackId(
        std::string const&   pluginName,
        std::string const&   name,
        PlaceholderType      type,
        uint64_t             callbackId,
        int                  updateIntervalMs,
        bool                 mainThreadOnly
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
    // 占位符概要列表（按名字排序，/meowpapi list 分页展示用）
    std::vector<PlaceholderInfo> listPlaceholderInfos();

    // 设置回退解析器（当本地注册表找不到占位符时调用）
    // 用于 BEPAPI 双向兼容：BepApiBridge 设置此解析器，回退到 BEPAPI 查询
    using FallbackResolver = std::function<std::string(std::string const& name, Player* player)>;
    void setFallbackResolver(FallbackResolver resolver);

    // 设置回调调用器（用于 DLL 边界外的回调）
    // 当占位符条目的 callbackId 非零时，通过此函数指针回调静态库侧
    using CallbackInvoker = void(*)(uint64_t callbackId, void* player, char* out, int outSize);
    void setCallbackInvoker(CallbackInvoker invoker);

    // 带参数回调调用器（DLL 边界外的带参回调）
    // 当占位符条目 callbackId 非零且 hasParams=true 时使用
    using CallbackInvokerWithParams =
        void(*)(uint64_t callbackId, void* player, const char* paramsJson, char* out, int outSize);
    void setCallbackInvokerWithParams(CallbackInvokerWithParams invoker);

private:
    PlaceholderRegistry() = default;

    std::unordered_map<std::string, PlaceholderEntry> mPlaceholders;
    FallbackResolver                                  mFallbackResolver;
    CallbackInvoker                                   mCallbackInvoker = nullptr;
    CallbackInvokerWithParams                         mCallbackInvokerWithParams = nullptr;

    // 更新静态占位符缓存
    void updateStaticCache(PlaceholderEntry const& entry) const;

    // 调用占位符回调（自动选择 std::function 或 callbackId 回调调用器）
    std::string invokeEntryCallback(PlaceholderEntry const& entry, Player* player) const;

    // 调用占位符回调（带参数版本：paramCallback 优先，其次 callbackId 带参 invoker，
    // 最后退化为普通 callback——普通回调忽略参数）
    std::string invokeEntryCallbackWithParams(
        PlaceholderEntry const& entry, Player* player, std::string const& paramsJson
    ) const;

    // 解析单个占位符名称（不含定界符）
    // skipRemote=true 时跳过 mainThreadOnly 占位符（保留原始占位符文本）
    std::string resolvePlaceholder(std::string const& name, Player* player, bool skipRemote);

    // 解析核心：精确匹配 → 槽位模板匹配（GMLIB <param> 兼容）→ 返回是否命中
    // paramsJson 为空指针表示无显式参数（%name% / {name} 简单格式）
    bool resolvePlaceholderInternal(
        std::string const&  name,
        Player*             player,
        bool                skipRemote,
        std::string const*  paramsJson,
        std::string&        outResult
    );

    // 翻译 ${papi:...} 表达式内部内容（不含 "${papi:" 前缀和 "}" 后缀）
    // 解析变量名、参数列表（key=value、多键同值、\, 转义、嵌套 ${...} 递归翻译）
    // 返回 true 表示已解析（结果写入 outResult）；false 表示保留原文
    bool translatePapiExpression(
        std::string const&  inner,
        Player*             player,
        bool                skipRemote,
        std::string&        outResult
    );
};

} // namespace meowpapi
