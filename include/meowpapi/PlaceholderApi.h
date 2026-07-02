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

private:
    PlaceholderApi() = default;
    bool mIsServer      = false;
    bool mInitialized   = false;
};

} // namespace meowpapi
