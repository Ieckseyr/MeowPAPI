// RemoteCallBridge.cpp - RemoteCall 桥接层（静态库包装层）
//
// 转发到 MeowPAPI.dll 的导出函数。
// 静态库不依赖 legacyremotecall，所有 RemoteCall 调用都在 DLL 内部完成。
#include "meowpapi/RemoteCallBridge.h"
#include "DllLoader.h"

#include "mc/world/actor/player/Player.h"

#include <string>

namespace meowpapi {

namespace {

DllFunctions const* getDllFuncs() {
    auto& loader = DllLoader::getInstance();
    if (!loader.load()) return nullptr;
    return loader.functions();
}

std::string callStr(int (*fn)(const char*, char*, int), const char* input) {
    if (!fn) return input ? std::string(input) : std::string();
    int len = fn(input, nullptr, 0);
    if (len <= 0) return std::string();
    std::string result(len, '\0');
    fn(input, result.data(), len + 1);
    return result;
}

std::string callStrPlayerName(
    int (*fn)(const char*, const char*, char*, int),
    const char* input,
    const char* playerName
) {
    if (!fn) return input ? std::string(input) : std::string();
    int len = fn(input, playerName, nullptr, 0);
    if (len <= 0) return std::string();
    std::string result(len, '\0');
    fn(input, playerName, result.data(), len + 1);
    return result;
}

} // anonymous namespace

// 服务端 API
void exportRemoteCallApi() {
    auto& loader = DllLoader::getInstance();
    if (!loader.load()) return;
    // 自动初始化模式下已由 DllLoader 调用，跳过
    if (loader.isAutoInitialized()) return;
    if (auto* f = loader.functions()) f->exportRemoteCallApi();
}

void removeRemoteCallApi() {
    if (auto* f = getDllFuncs()) f->removeRemoteCallApi();
}

// 客户端 API
bool isRemoteAvailable() {
    auto* f = getDllFuncs();
    return f ? (f->isRemoteAvailable() != 0) : false;
}

std::string remoteTranslateString(std::string const& str) {
    auto* f = getDllFuncs();
    if (!f) return str;
    return callStr(f->remoteTranslateString, str.c_str());
}

std::string remoteTranslateStringWithPlayer(std::string const& str, Player* player) {
    auto* f = getDllFuncs();
    if (!f) return str;
    if (!player) return callStr(f->remoteTranslateString, str.c_str());
    // 通过玩家名调用
    auto name = player->getRealName();
    return callStrPlayerName(f->remoteTranslateStringWithPlayer, str.c_str(), name.c_str());
}

std::string remoteGetValue(std::string const& name) {
    auto* f = getDllFuncs();
    if (!f) return "";
    return callStr(f->remoteGetValue, name.c_str());
}

std::string remoteGetValueWithPlayer(std::string const& name, Player* player) {
    auto* f = getDllFuncs();
    if (!f) return "";
    if (!player) return callStr(f->remoteGetValue, name.c_str());
    auto playerName = player->getRealName();
    return callStrPlayerName(f->remoteGetValueWithPlayer, name.c_str(), playerName.c_str());
}

bool remoteHasPlaceholder(std::string const& name) {
    auto* f = getDllFuncs();
    return f ? (f->remoteHasPlaceholder(name.c_str()) != 0) : false;
}

// 通过玩家名调用（DLL 导出层使用）
std::string remoteTranslateStringWithPlayerName(std::string const& str, std::string const& playerName) {
    auto* f = getDllFuncs();
    if (!f) return str;
    return callStrPlayerName(f->remoteTranslateStringWithPlayer, str.c_str(), playerName.c_str());
}

std::string remoteGetValueWithPlayerName(std::string const& name, std::string const& playerName) {
    auto* f = getDllFuncs();
    if (!f) return "";
    return callStrPlayerName(f->remoteGetValueWithPlayer, name.c_str(), playerName.c_str());
}

} // namespace meowpapi
