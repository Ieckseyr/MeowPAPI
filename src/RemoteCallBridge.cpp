// RemoteCallBridge.cpp - RemoteCall 桥接层实现
#include "meowpapi/RemoteCallBridge.h"
#include "meowpapi/PlaceholderRegistry.h"

#include <RemoteCallAPI.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "mc/world/actor/player/Player.h"
#include "ll/api/service/Bedrock.h"
#include "mc/world/level/Level.h"

namespace meowpapi {

//服务端：导出 PAPI 操作到 RemoteCall

namespace {

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
        auto fn = RemoteCall::importAs<std::string()>(callbackPluginName, callbackFuncName);
        return fn();
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
        auto fn = RemoteCall::importAs<std::string(Player*)>(callbackPluginName, callbackFuncName);
        return fn(player);
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
        auto fn = RemoteCall::importAs<std::string()>(callbackPluginName, callbackFuncName);
        return fn();
    };
    return PlaceholderRegistry::getInstance().registerStaticPlaceholderRemote(
        pluginName, papiName, cb, updateIntervalMs
    );
}

// 通过玩家名获取 Player*
Player* findPlayerByName(std::string const& name) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    return level->getPlayer(name);
}

} // anonymous namespace

void exportRemoteCallApi() {
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

} // namespace meowpapi
