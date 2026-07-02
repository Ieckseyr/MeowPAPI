// RemoteCallBridge.h - RemoteCall 桥接层
//
// 双向桥接：
// - 服务端（MeowSidebar）：通过 RemoteCall 导出所有 PAPI 操作，供 LSE/C++ 插件调用
// - 客户端（MeowMenu 等）：通过 RemoteCall 调用 MeowSidebar 的 PAPI 服务
//
// LSE 插件注册 PAPI 的流程：
// 1. ll.export(func, PluginName, funcName)  导出回调
// 2. ll.import("MeowSidebar", "registerServerPlaceholder")(PluginName, funcName, PAPIName)
// 3. MeowSidebar 在求值时通过 RemoteCall::importAs 调用回调
#pragma once

#include <string>

class Player;

namespace meowpapi {

// RemoteCall 命名空间
// JS 插件通过 ll.import("MeowPAPI", "registerPlayerPlaceholder") 等调用
constexpr char const* REMOTE_NS = "MeowPAPI";

//===== 服务端 API（MeowSidebar 调用）=====

// 导出所有 PAPI 操作到 RemoteCall
// MeowSidebar 在 enable 阶段调用
void exportRemoteCallApi();

// 移除所有 RemoteCall 导出
// MeowSidebar 在 disable 阶段调用
void removeRemoteCallApi();

//===== 客户端 API（其他 C++ 插件调用）=====

// 检查 MeowSidebar 是否已加载（RemoteCall 函数是否可用）
bool isRemoteAvailable();

// 通过 RemoteCall 调用 MeowSidebar 的 PAPI 服务
// 如果 MeowSidebar 未加载，返回空字符串/false
std::string remoteTranslateString(std::string const& str);
std::string remoteTranslateStringWithPlayer(std::string const& str, Player* player);
std::string remoteGetValue(std::string const& name);
std::string remoteGetValueWithPlayer(std::string const& name, Player* player);
bool        remoteHasPlaceholder(std::string const& name);

} // namespace meowpapi
