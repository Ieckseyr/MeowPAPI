// PlaceholderApi.h - 统一 PAPI 入口
//
// 自动路由：
// - 服务端模式（MeowSidebar）：直接使用本地 PlaceholderRegistry
// - 客户端模式（MeowMenu 等）：通过 RemoteCall 调用 MeowSidebar
//
// 使用方式：
//   // MeowSidebar 中：
//   meowpapi::PlaceholderApi::getInstance().initAsServer();
//
//   // MeowMenu 中：
//   meowpapi::PlaceholderApi::getInstance().initAsClient();
//   std::string resolved = meowpapi::PlaceholderApi::getInstance().translateStringWithPlayer(text, player);
#pragma once

#include "meowpapi/PlaceholderRegistry.h"

#include <string>

class Player;

namespace meowpapi {

class PlaceholderApi {
public:
    static PlaceholderApi& getInstance();

    // 初始化为服务端模式（MeowSidebar 调用）
    // 直接使用本地 PlaceholderRegistry
    void initAsServer();

    // 初始化为客户端模式（其他 C++ 插件调用）
    // 通过 RemoteCall 调用 MeowSidebar
    void initAsClient();

    // 翻译字符串（自动路由）
    std::string translateString(std::string const& str);
    std::string translateStringWithPlayer(std::string const& str, Player* player);
    // 带 skipRemote 参数的翻译：skipRemote=true 时跳过 mainThreadOnly 占位符
    // （仅在服务端模式生效；客户端模式忽略此参数）
    std::string translateStringWithPlayer(std::string const& str, Player* player, bool skipRemote);

    // 获取单个占位符值
    std::string getValue(std::string const& name);
    std::string getValueWithPlayer(std::string const& name, Player* player);

    // 检查占位符是否存在
    bool hasPlaceholder(std::string const& name);

    // 注册占位符
    // 服务端模式：直接注册到本地 PlaceholderRegistry
    // 客户端模式：通过 RemoteCall 导出到 MeowSidebar
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

    // 带参数注册（GMLIB PAPI 兼容，add-only 扩展）
    // name 可含 <param> 插槽模板（如 "title_score_rank_<score>_<title>_<number>"），
    // 翻译时按 _ 分段匹配实际占位符名提取参数；回调通过 paramsJson 收到参数
    // （JSON 对象字符串，如 {"score":"zxsc","title":"在线榜","number":"10"}）
    // 翻译格式：${papi:title_score_rank_<score>_<title>_<number>,<score>=zxsc,...}
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

    //===== ABI 查询 =====
    // DLL 侧实现：返回本 DLL 编译期的 MEOWPAPI_ABI_VERSION（始终为编译期宏值）
    // wrapper 侧实现：返回已加载 MeowPAPI.dll 运行时报告的 ABI 版本
    //                 （旧版 DLL 无 ABI 导出返回 0，消费者据此降级）
    // 编译期常量用 MeowPAPI_GetAbiVersion() C API 或 MEOWPAPI_ABI_VERSION 宏获取
    uint32_t getAbiVersion();

    // 便捷判断：当前运行环境是否支持带参占位符（GMLIB PAPI 兼容）
    // DLL 侧：恒为 true（本 DLL 编译期已含此功能）
    // wrapper 侧：查询已加载 DLL 的 ABI 功能位（旧版 DLL 返回 false）
    bool isParamPapiSupported();

private:
    PlaceholderApi() = default;
    bool mIsServer      = false;
    bool mInitialized   = false;
};

} // namespace meowpapi
