// RemoteCallBridge.cpp - RemoteCall 桥接层实现
//
// 本文件在 MeowPAPI_DLL target 中编译（定义 MEOWPAPI_DLL_EXPORTS），
// 因此 meowpapi/RemoteCallAPI.h 走 DLL 模式：RemoteCall::exportAs 等函数
// 通过 __declspec(dllimport) 直接从 LegacyRemoteCall.dll 导入。
//
// MeowPAPI.dll 导入表硬依赖 lrca，Windows 加载器保证 lrca 先加载，
// 故此处无需运行时动态解析（LrcaLoader 已移除），也无需 ServerStartedEvent
// 重试机制——enable() 阶段 lrca 必定已就绪。
#include "meowpapi/RemoteCallBridge.h"
#include "meowpapi/DllExports.h"
#include "meowpapi/PlaceholderRegistry.h"
#include "meowpapi/RemoteCallAPI.h"

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <mutex>

#include "mc/world/actor/player/Player.h"
#include "ll/api/service/Bedrock.h"
#include "mc/world/level/Level.h"

namespace meowpapi {

//服务端：导出 PAPI 操作到 RemoteCall

namespace {

// 幂等保护：确保 MeowPAPI 自身的 RemoteCall API 只导出一次。
// 硬依赖方案下 lrca 在 MeowPAPI.dll 加载前已就绪，无需重试。
std::once_flag g_exportOnce;

// 远程占位符回调的安全调用：
// 使用底层 importFunc 而非 importAs——目标回调不存在时（典型场景：关服时
// LSE 引擎先卸载 More_PAPI 等脚本插件，ll.exports 导出已随插件移除，
// 而本插件注册表中仍保留占位符条目，消费者最后一次翻译会触发调用）
// 静默返回空串，不走 importAs 的 _onCallError 报错路径，
// 避免 "Fail to import! Function [xx::yy] has not been exported" 关服错误日志。
// 目标插件热重载后 importFunc 重新可见，占位符自动恢复工作。

// 服务器级/静态级：无参调用
std::string callRemoteCallbackNoArgs(std::string const& callbackPluginName, std::string const& callbackFuncName) {
    auto const& rawFunc = RemoteCall::importFunc(callbackPluginName, callbackFuncName);
    if (!rawFunc) return "";
    std::vector<RemoteCall::ValueType> params;
    auto result = rawFunc(std::move(params));
    return RemoteCall::extract<std::string>(std::move(result));
}

// 玩家级：单 Player* 参数
std::string callRemoteCallbackWithPlayer(
    std::string const& callbackPluginName,
    std::string const& callbackFuncName,
    Player*            player
) {
    auto const& rawFunc = RemoteCall::importFunc(callbackPluginName, callbackFuncName);
    if (!rawFunc) return "";
    std::vector<RemoteCall::ValueType> params = {RemoteCall::pack(player)};
    auto result = rawFunc(std::move(params));
    return RemoteCall::extract<std::string>(std::move(result));
}

// 带参级：单 paramsJson 参数（服务器级带参占位符）
// LSE 回调签名：(paramsJson: string) -> string
std::string callRemoteCallbackWithParams(
    std::string const& callbackPluginName,
    std::string const& callbackFuncName,
    std::string const& paramsJson
) {
    auto const& rawFunc = RemoteCall::importFunc(callbackPluginName, callbackFuncName);
    if (!rawFunc) return "";
    std::vector<RemoteCall::ValueType> params = {RemoteCall::pack(paramsJson)};
    auto result = rawFunc(std::move(params));
    return RemoteCall::extract<std::string>(std::move(result));
}

// 带参级：Player* + paramsJson（玩家级带参占位符）
// LSE 回调签名：(player, paramsJson: string) -> string
std::string callRemoteCallbackWithPlayerAndParams(
    std::string const& callbackPluginName,
    std::string const& callbackFuncName,
    Player*            player,
    std::string const& paramsJson
) {
    auto const& rawFunc = RemoteCall::importFunc(callbackPluginName, callbackFuncName);
    if (!rawFunc) return "";
    std::vector<RemoteCall::ValueType> params = {RemoteCall::pack(player), RemoteCall::pack(paramsJson)};
    auto result = rawFunc(std::move(params));
    return RemoteCall::extract<std::string>(std::move(result));
}

// 远程注册的服务器级占位符包装器
// 通过 RemoteCall 调用 LSE 插件导出的回调函数
// 使用 *Remote 版本注册（设置 mainThreadOnly=true），后台线程翻译时跳过
bool registerServerPlaceholderRemote(
    std::string const& pluginName,
    std::string const& papiName,
    std::string const& callbackPluginName,
    std::string const& callbackFuncName
) {
    auto cb = [callbackPluginName, callbackFuncName](Player* /*player*/) -> std::string {
        return callRemoteCallbackNoArgs(callbackPluginName, callbackFuncName);
    };
    return PlaceholderRegistry::getInstance().registerServerPlaceholderRemote(pluginName, papiName, cb);
}

bool registerPlayerPlaceholderRemote(
    std::string const& pluginName,
    std::string const& papiName,
    std::string const& callbackPluginName,
    std::string const& callbackFuncName
) {
    auto cb = [callbackPluginName, callbackFuncName](Player* player) -> std::string {
        return callRemoteCallbackWithPlayer(callbackPluginName, callbackFuncName, player);
    };
    return PlaceholderRegistry::getInstance().registerPlayerPlaceholderRemote(pluginName, papiName, cb);
}

bool registerStaticPlaceholderRemote(
    std::string const& pluginName,
    std::string const& papiName,
    std::string const& callbackPluginName,
    std::string const& callbackFuncName,
    int                updateIntervalMs
) {
    auto cb = [callbackPluginName, callbackFuncName](Player* /*player*/) -> std::string {
        return callRemoteCallbackNoArgs(callbackPluginName, callbackFuncName);
    };
    return PlaceholderRegistry::getInstance().registerStaticPlaceholderRemote(
        pluginName, papiName, cb, updateIntervalMs
    );
}

// 远程注册的带参服务器级占位符包装器（GMLIB PAPI 兼容）
// papiName 可含 <param> 插槽模板（如 "title_score_rank_<score>_<title>_<number>"）
// LSE 回调签名：(paramsJson: string) -> string，paramsJson 为参数键值对 JSON 对象字符串
// 同样使用 *Remote 版本注册（mainThreadOnly=true）
bool registerServerPlaceholderWithParamsRemote(
    std::string const& pluginName,
    std::string const& papiName,
    std::string const& callbackPluginName,
    std::string const& callbackFuncName
) {
    auto cb = [callbackPluginName, callbackFuncName](
                  Player* /*player*/, std::string const& paramsJson
              ) -> std::string {
        return callRemoteCallbackWithParams(callbackPluginName, callbackFuncName, paramsJson);
    };
    return PlaceholderRegistry::getInstance().registerServerPlaceholderWithParamsRemote(pluginName, papiName, cb);
}

// 远程注册的带参玩家级占位符包装器（GMLIB PAPI 兼容）
// LSE 回调签名：(player, paramsJson: string) -> string
bool registerPlayerPlaceholderWithParamsRemote(
    std::string const& pluginName,
    std::string const& papiName,
    std::string const& callbackPluginName,
    std::string const& callbackFuncName
) {
    auto cb = [callbackPluginName, callbackFuncName](
                  Player* player, std::string const& paramsJson
              ) -> std::string {
        return callRemoteCallbackWithPlayerAndParams(callbackPluginName, callbackFuncName, player, paramsJson);
    };
    return PlaceholderRegistry::getInstance().registerPlayerPlaceholderWithParamsRemote(pluginName, papiName, cb);
}

// 通过玩家名获取 Player*
Player* findPlayerByName(std::string const& name) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    return level->getPlayer(name);
}

} // anonymous namespace

void exportRemoteCallApi() {
    // 幂等导出（只执行一次）
    // 硬依赖方案下 lrca 在 MeowPAPI.dll 加载前已就绪，直接导出即可
    std::call_once(g_exportOnce, []() {
        // ABI 查询（供 LSE 插件判断运行时能力）
        // ll.import("MeowPAPI", "getAbiVersion")() -> 0x010100
        RemoteCall::exportAs(REMOTE_NS, "getAbiVersion",
            []() -> int {
                return static_cast<int>(MEOWPAPI_ABI_VERSION);
            }
        );
        // ll.import("MeowPAPI", "getAbiFeatures")() -> 功能位掩码
        // 判断带参支持：(features & 1) != 0（bit0 = MEOWPAPI_ABI_FEATURE_PARAMS）
        RemoteCall::exportAs(REMOTE_NS, "getAbiFeatures",
            []() -> int {
                uint32_t features = 0;
                features |= MEOWPAPI_ABI_FEATURE_PARAMS;
                return static_cast<int>(features);
            }
        );

        // 注册占位符（供 LSE 插件调用）
        RemoteCall::exportAs(REMOTE_NS, "registerServerPlaceholder",
            [](std::string const& pluginName, std::string const& papiName,
               std::string const& callbackPluginName, std::string const& callbackFuncName) -> bool {
                return registerServerPlaceholderRemote(pluginName, papiName, callbackPluginName, callbackFuncName);
            }
        );

        RemoteCall::exportAs(REMOTE_NS, "registerPlayerPlaceholder",
            [](std::string const& pluginName, std::string const& papiName,
               std::string const& callbackPluginName, std::string const& callbackFuncName) -> bool {
                return registerPlayerPlaceholderRemote(pluginName, papiName, callbackPluginName, callbackFuncName);
            }
        );

        RemoteCall::exportAs(REMOTE_NS, "registerStaticPlaceholder",
            [](std::string const& pluginName, std::string const& papiName,
               std::string const& callbackPluginName, std::string const& callbackFuncName,
               int updateIntervalMs) -> bool {
                return registerStaticPlaceholderRemote(
                    pluginName, papiName, callbackPluginName, callbackFuncName, updateIntervalMs
                );
            }
        );

        // 带参注册（GMLIB PAPI 兼容，add-only 扩展）
        // papiName 可含 <param> 插槽模板；求值时回调收到参数 JSON 对象字符串
        RemoteCall::exportAs(REMOTE_NS, "registerServerPlaceholderWithParams",
            [](std::string const& pluginName, std::string const& papiName,
               std::string const& callbackPluginName, std::string const& callbackFuncName) -> bool {
                return registerServerPlaceholderWithParamsRemote(
                    pluginName, papiName, callbackPluginName, callbackFuncName
                );
            }
        );

        RemoteCall::exportAs(REMOTE_NS, "registerPlayerPlaceholderWithParams",
            [](std::string const& pluginName, std::string const& papiName,
               std::string const& callbackPluginName, std::string const& callbackFuncName) -> bool {
                return registerPlayerPlaceholderWithParamsRemote(
                    pluginName, papiName, callbackPluginName, callbackFuncName
                );
            }
        );

        // 注销占位符
        RemoteCall::exportAs(REMOTE_NS, "unRegisterPlaceholder",
            [](std::string const& papiName) -> bool {
                return PlaceholderRegistry::getInstance().unregisterPlaceholder(papiName);
            }
        );

        // 获取值
        RemoteCall::exportAs(REMOTE_NS, "GetValue",
            [](std::string const& papiName) -> std::string {
                return PlaceholderRegistry::getInstance().getValue(papiName);
            }
        );

        RemoteCall::exportAs(REMOTE_NS, "GetValueWithPlayer",
            [](std::string const& papiName, std::string const& playerName) -> std::string {
                auto* player = findPlayerByName(playerName);
                return PlaceholderRegistry::getInstance().getValueWithPlayer(papiName, player);
            }
        );

        // 翻译字符串
        RemoteCall::exportAs(REMOTE_NS, "translateString",
            [](std::string const& str) -> std::string {
                return PlaceholderRegistry::getInstance().translateString(str);
            }
        );

        RemoteCall::exportAs(REMOTE_NS, "translateStringWithPlayer",
            [](std::string const& str, std::string const& playerName) -> std::string {
                auto* player = findPlayerByName(playerName);
                return PlaceholderRegistry::getInstance().translateStringWithPlayer(str, player);
            }
        );

        // 检查占位符是否存在
        RemoteCall::exportAs(REMOTE_NS, "hasPlaceholder",
            [](std::string const& papiName) -> bool {
                return PlaceholderRegistry::getInstance().hasPlaceholder(papiName);
            }
        );

        // 列出所有占位符
        RemoteCall::exportAs(REMOTE_NS, "listPlaceholders",
            []() -> std::string {
                auto list = PlaceholderRegistry::getInstance().listPlaceholders();
                nlohmann::json j = nlohmann::json::array();
                for (auto const& name : list) j.push_back(name);
                return j.dump();
            }
        );
    });
}

void removeRemoteCallApi() {
    RemoteCall::removeNameSpace(REMOTE_NS);
}

//客户端：通过 RemoteCall 调用 MeowSidebar

bool isRemoteAvailable() {
    return RemoteCall::hasFunc(REMOTE_NS, "translateString");
}

std::string remoteTranslateString(std::string const& str) {
    if (!isRemoteAvailable()) return str;
    auto fn = RemoteCall::importAs<std::string(std::string const&)>(REMOTE_NS, "translateString");
    return fn(str);
}

std::string remoteTranslateStringWithPlayer(std::string const& str, Player* player) {
    if (!isRemoteAvailable()) return str;
    if (!player) return remoteTranslateString(str);
    // 通过玩家名调用
    auto name = player->getRealName();
    auto fn = RemoteCall::importAs<std::string(std::string const&, std::string const&)>(
        REMOTE_NS, "translateStringWithPlayer"
    );
    return fn(str, name);
}

std::string remoteGetValue(std::string const& name) {
    if (!isRemoteAvailable()) return "";
    auto fn = RemoteCall::importAs<std::string(std::string const&)>(REMOTE_NS, "GetValue");
    return fn(name);
}

std::string remoteGetValueWithPlayer(std::string const& name, Player* player) {
    if (!isRemoteAvailable()) return "";
    if (!player) return remoteGetValue(name);
    auto playerName = player->getRealName();
    auto fn = RemoteCall::importAs<std::string(std::string const&, std::string const&)>(
        REMOTE_NS, "GetValueWithPlayer"
    );
    return fn(name, playerName);
}

bool remoteHasPlaceholder(std::string const& name) {
    if (!isRemoteAvailable()) return false;
    auto fn = RemoteCall::importAs<bool(std::string const&)>(REMOTE_NS, "hasPlaceholder");
    return fn(name);
}

//通过玩家名调用（DLL 导出层使用）

std::string remoteTranslateStringWithPlayerName(std::string const& str, std::string const& playerName) {
    if (!isRemoteAvailable()) return str;
    auto fn = RemoteCall::importAs<std::string(std::string const&, std::string const&)>(
        REMOTE_NS, "translateStringWithPlayer"
    );
    return fn(str, playerName);
}

std::string remoteGetValueWithPlayerName(std::string const& name, std::string const& playerName) {
    if (!isRemoteAvailable()) return "";
    auto fn = RemoteCall::importAs<std::string(std::string const&, std::string const&)>(
        REMOTE_NS, "GetValueWithPlayer"
    );
    return fn(name, playerName);
}

} // namespace meowpapi
