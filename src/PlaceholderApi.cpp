// PlaceholderApi.cpp - 统一 PAPI 入口实现
#include "meowpapi/PlaceholderApi.h"
#include "meowpapi/PlaceholderRegistry.h"
#include "meowpapi/RemoteCallBridge.h"

#include <RemoteCallAPI.h>

#include "mc/world/actor/player/Player.h"

namespace meowpapi {

PlaceholderApi& PlaceholderApi::getInstance() {
    static PlaceholderApi instance;
    return instance;
}

void PlaceholderApi::initAsServer() {
    mIsServer     = true;
    mInitialized  = true;
}

void PlaceholderApi::initAsClient() {
    mIsServer     = false;
    mInitialized  = true;
}

std::string PlaceholderApi::translateString(std::string const& str) {
    if (mIsServer) {
        return PlaceholderRegistry::getInstance().translateString(str);
    }
    return remoteTranslateString(str);
}

std::string PlaceholderApi::translateStringWithPlayer(std::string const& str, Player* player) {
    if (mIsServer) {
        return PlaceholderRegistry::getInstance().translateStringWithPlayer(str, player);
    }
    return remoteTranslateStringWithPlayer(str, player);
}

std::string PlaceholderApi::translateStringWithPlayer(std::string const& str, Player* player, bool skipRemote) {
    if (mIsServer) {
        return PlaceholderRegistry::getInstance().translateStringWithPlayer(str, player, skipRemote);
    }
    // 客户端模式：RemoteCall 总是在调用方线程执行，无 skipRemote 概念
    (void)skipRemote;
    return remoteTranslateStringWithPlayer(str, player);
}

std::string PlaceholderApi::getValue(std::string const& name) {
    if (mIsServer) {
        return PlaceholderRegistry::getInstance().getValue(name);
    }
    return remoteGetValue(name);
}

std::string PlaceholderApi::getValueWithPlayer(std::string const& name, Player* player) {
    if (mIsServer) {
        return PlaceholderRegistry::getInstance().getValueWithPlayer(name, player);
    }
    return remoteGetValueWithPlayer(name, player);
}

bool PlaceholderApi::hasPlaceholder(std::string const& name) {
    if (mIsServer) {
        return PlaceholderRegistry::getInstance().hasPlaceholder(name);
    }
    return remoteHasPlaceholder(name);
}

bool PlaceholderApi::registerServerPlaceholder(
    std::string const& pluginName, std::string const& name, PlaceholderCallback cb
) {
    if (mIsServer) {
        return PlaceholderRegistry::getInstance().registerServerPlaceholder(pluginName, name, std::move(cb));
    }
    // 客户端模式：导出回调到 RemoteCall，然后通过 RemoteCall 注册到 MeowSidebar
    if (!isRemoteAvailable()) return false;
    std::string callbackNs = pluginName;
    std::string callbackFn = "papi_srv_" + name;
    RemoteCall::exportAs(callbackNs, callbackFn,
        [cb = std::move(cb)]() -> std::string { return cb(nullptr); }
    );
    auto registerFn = RemoteCall::importAs<
        bool(std::string const&, std::string const&, std::string const&, std::string const&)
    >(REMOTE_NS, "registerServerPlaceholder");
    return registerFn(pluginName, name, callbackNs, callbackFn);
}

bool PlaceholderApi::registerPlayerPlaceholder(
    std::string const& pluginName, std::string const& name, PlaceholderCallback cb
) {
    if (mIsServer) {
        return PlaceholderRegistry::getInstance().registerPlayerPlaceholder(pluginName, name, std::move(cb));
    }
    if (!isRemoteAvailable()) return false;
    std::string callbackNs = pluginName;
    std::string callbackFn = "papi_pl_" + name;
    RemoteCall::exportAs(callbackNs, callbackFn,
        [cb = std::move(cb)](Player* player) -> std::string { return cb(player); }
    );
    auto registerFn = RemoteCall::importAs<
        bool(std::string const&, std::string const&, std::string const&, std::string const&)
    >(REMOTE_NS, "registerPlayerPlaceholder");
    return registerFn(pluginName, name, callbackNs, callbackFn);
}

bool PlaceholderApi::registerStaticPlaceholder(
    std::string const& pluginName, std::string const& name, PlaceholderCallback cb, int updateIntervalMs
) {
    if (mIsServer) {
        return PlaceholderRegistry::getInstance().registerStaticPlaceholder(
            pluginName, name, std::move(cb), updateIntervalMs
        );
    }
    if (!isRemoteAvailable()) return false;
    std::string callbackNs = pluginName;
    std::string callbackFn = "papi_st_" + name;
    RemoteCall::exportAs(callbackNs, callbackFn,
        [cb = std::move(cb)]() -> std::string { return cb(nullptr); }
    );
    auto registerFn = RemoteCall::importAs<
        bool(std::string const&, std::string const&, std::string const&, std::string const&, int)
    >(REMOTE_NS, "registerStaticPlaceholder");
    return registerFn(pluginName, name, callbackNs, callbackFn, updateIntervalMs);
}

} // namespace meowpapi
