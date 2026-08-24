// RemoteCallBridge.cpp - RemoteCall 桥接层实现
//
// 本文件在 MeowPAPI_DLL target 中编译（定义 MEOWPAPI_DLL_EXPORTS），
// 因此 meowpapi/RemoteCallAPI.h 走 DLL 模式：RemoteCall::exportAs 等函数
// 经 src/lse/LseBridge.cpp 运行时挂载 lrca（软依赖，导入表无 lrca）。
//
// 挂载时序：
// - lrca 先加载（名称序 LegacyRemoteCall < MeowPAPI；或消费者插件 load 阶段
//   触发部署时 lrca DLL 已映射进进程）→ attach() 立即成功，直接导出
// - lrca 尚未加载 → 挂 ServerStartedEvent 兜底重试（届时所有插件均已加载）。
//   期间原生 C++ 占位符 API（PlaceholderApi/PlaceholderRegistry）正常工作，
//   仅 LSE (ll.import "MeowPAPI"::*.) 调用暂不可用；后续调用方再触发
//   exportRemoteCallApi 时也会即时补导出
#include "meowpapi/RemoteCallBridge.h"
#include "meowpapi/DllExports.h"
#include "meowpapi/PlaceholderRegistry.h"
#include "meowpapi/RemoteCallAPI.h"
#include "lse/LseBridge.h"

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include "mc/world/actor/player/Player.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/server/ServerStartedEvent.h"
#include "mc/world/level/Level.h"

namespace meowpapi {

//服务端：导出 PAPI 操作到 RemoteCall

namespace {

// 幂等保护：确保挂载/兜底监听逻辑只执行一次；实际导出由 gLseApiExported 幂等
std::once_flag g_exportOnce;

// LSE API 实际导出完成标志（attach 成功且 14 个 exportAs 已注册）
std::atomic<bool> gLseApiExported{false};

// ServerStarted 兜底监听器（lrca 后加载时补导出；disable 时移除）
ll::event::ListenerPtr gRetryListener;

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

// 实际导出 LSE API（幂等，attach 成功后调用）
void doExportLseApi() {
    if (gLseApiExported.exchange(true)) return;
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
    // ll.import("MeowPAPI", "getVersion")() -> 版本字符串（如 "1.1.0"）
    // LSE 插件启动横幅/诊断用，与 getAbiVersion 数值版配对
    RemoteCall::exportAs(REMOTE_NS, "getVersion",
        []() -> std::string {
            return MEOWPAPI_VERSION_STRING;
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
}

} // anonymous namespace

void exportRemoteCallApi() {
    // 首次调用：lrca 已加载则立即导出；未加载则启动后台重试线程 +
    // ServerStartedEvent 兜底（双保险，doExportLseApi 幂等）
    std::call_once(g_exportOnce, []() {
        if (lse::attach()) {
            doExportLseApi();
            return;
        }
        // lrca 尚未加载（软依赖，嵌入释放模式下 manifest 依赖排序不生效）。
        //
        // 仅靠 ServerStarted 兜底不够：LeviLamina EventBus 按注册顺序触发，
        // LSE（legacy-script-engine）先于本插件加载时，其事件转发监听器先注册
        // → ServerStarted 触发时 JS 的 onServerStarted 先执行、本插件兜底后执行
        // → JS 的 ll.import/ll.hasExported("MeowPAPI", ...) 全部落空，报
        //   "Fail to import! Function [MeowPAPI::registerXxx] has not been
        //    exported!" / "MeowPAPI 不存在"。
        //
        // 修复：后台线程 200ms 轮询 attach，lrca 一加载（通常在插件加载阶段，
        // 早于 ServerStarted 数秒）立即导出，抢在 LSE 转发事件之前完成。
        // lrca 的 exportFunc 内部有锁（跨线程设计），线程安全。
        std::thread([]() {
            for (int i = 0; i < 150 && !gLseApiExported.load(); i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                if (lse::attach()) {
                    doExportLseApi();
                    break;
                }
            }
        }).detach();
        // ServerStarted 兜底保留（重试线程之外的保险；两者经 gLseApiExported 幂等）。
        // 监听器须持有 ListenerPtr，否则 shared_ptr 引用归零监听器立即销毁
        gRetryListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerStartedEvent>(
            [](ll::event::ServerStartedEvent&) {
                if (lse::attach()) doExportLseApi();
            }
        );
    });
    // 后续调用（call_once 已消费）时 lrca 可能已加载：即时补导出
    if (!gLseApiExported.load() && lse::attach()) {
        doExportLseApi();
    }
}

void removeRemoteCallApi() {
    // 移除兜底监听器（若已挂载且尚未触发）
    if (gRetryListener) {
        ll::event::EventBus::getInstance().removeListener(gRetryListener);
        gRetryListener = nullptr;
    }
    // lrca 未挂载时安全空操作
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
