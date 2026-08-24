// PlaceholderApi.cpp - 统一 PAPI 入口（静态库包装层）
//
// 此文件编译到静态库中，不包含实际逻辑。
// 所有调用转发到 MeowPAPI.dll 导出的 C API。
// 静态库从头到尾只是中间层，不依赖 legacyremotecall。
#include "meowpapi/PlaceholderApi.h"
#include "meowpapi/PlaceholderRegistry.h"
#include "DllLoader.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>

namespace meowpapi {

namespace {

// 诊断日志：写入文件 meowpapi_loader_debug.log
void debugLog(char const* fmt, ...) {
    try {
        char buf[2048];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        auto now  = std::chrono::system_clock::now();
        auto t    = std::chrono::system_clock::to_time_t(now);
        auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        struct tm tmv;
        localtime_s(&tmv, &t);

        char line[2300];
        int prefixLen = snprintf(line, sizeof(line),
            "[%04d-%02d-%02d %02d:%02d:%02d.%03d][PlaceholderApi] ",
            tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
            tmv.tm_hour, tmv.tm_min, tmv.tm_sec, (int)ms.count());
        snprintf(line + prefixLen, sizeof(line) - prefixLen, "%s\n", buf);

        static std::mutex logMtx;
        std::lock_guard<std::mutex> lock(logMtx);
        std::ofstream f("meowpapi_loader_debug.log", std::ios::app);
        if (f.is_open()) {
            f.write(line, strlen(line));
        }
    } catch (...) {}
}

// 获取 DLL 函数指针表，如果 DLL 未加载则触发加载
DllFunctions const* getDllFuncs() {
    auto& loader = DllLoader::getInstance();
    if (!loader.load()) {
        static std::once_flag flag;
        std::call_once(flag, [] {
            debugLog("getDllFuncs: loader.load() FAILED, returning nullptr");
        });
        return nullptr;
    }
    auto* f = loader.functions();
    if (!f) {
        static std::once_flag flag;
        std::call_once(flag, [] {
            debugLog("getDllFuncs: loader.functions() returned nullptr");
        });
    }
    return f;
}

// 调用 DLL 的字符串返回函数并转换为 std::string
std::string callStringFunc(
    int (*fn)(const char*, char*, int),
    const char* input
) {
    if (!fn) return input ? std::string(input) : std::string();
    // 先获取所需长度
    int len = fn(input, nullptr, 0);
    if (len <= 0) return std::string();
    std::string result(len, '\0');
    fn(input, result.data(), len + 1);
    return result;
}

// 调用 DLL 的字符串返回函数（带 player 参数）
std::string callStringFuncPlayer(
    int (*fn)(const char*, void*, char*, int),
    const char* input,
    Player* player
) {
    if (!fn) return input ? std::string(input) : std::string();
    int len = fn(input, reinterpret_cast<void*>(player), nullptr, 0);
    if (len <= 0) return std::string();
    std::string result(len, '\0');
    fn(input, reinterpret_cast<void*>(player), result.data(), len + 1);
    return result;
}

// 调用 DLL 的字符串返回函数（带 player + skipRemote 参数）
std::string callStringFuncPlayerSkip(
    int (*fn)(const char*, void*, int, char*, int),
    const char* input,
    Player* player,
    bool skipRemote
) {
    if (!fn) return input ? std::string(input) : std::string();
    int len = fn(input, reinterpret_cast<void*>(player), skipRemote ? 1 : 0, nullptr, 0);
    if (len <= 0) return std::string();
    std::string result(len, '\0');
    fn(input, reinterpret_cast<void*>(player), skipRemote ? 1 : 0, result.data(), len + 1);
    return result;
}

} // anonymous namespace

PlaceholderApi& PlaceholderApi::getInstance() {
    static PlaceholderApi instance;
    return instance;
}

void PlaceholderApi::initAsServer() {
    mIsServer     = true;
    mInitialized  = true;
    debugLog("initAsServer: called, isAutoInit=%d", DllLoader::getInstance().isAutoInitialized() ? 1 : 0);
    // 若 DllLoader 已自动初始化（嵌入部署模式），跳过 DLL 调用
    if (DllLoader::getInstance().isAutoInitialized()) return;
    if (auto* f = getDllFuncs()) {
        debugLog("initAsServer: calling DLL f->initAsServer()");
        f->initAsServer();
        debugLog("initAsServer: DLL f->initAsServer() done");
    } else {
        debugLog("initAsServer: getDllFuncs() returned nullptr!");
    }
}

void PlaceholderApi::initAsClient() {
    // 嵌入部署模式下，DLL 已初始化为服务端，强制使用服务端模式
    if (DllLoader::getInstance().isAutoInitialized()) {
        mIsServer    = true;
        mInitialized = true;
        return;
    }
    mIsServer     = false;
    mInitialized  = true;
    if (auto* f = getDllFuncs()) {
        f->initAsClient();
    }
}

std::string PlaceholderApi::translateString(std::string const& str) {
    auto* f = getDllFuncs();
    if (!f) return str;
    return callStringFunc(f->translateString, str.c_str());
}

std::string PlaceholderApi::translateStringWithPlayer(std::string const& str, Player* player) {
    auto* f = getDllFuncs();
    if (!f) return str;
    static std::once_flag flag;
    std::call_once(flag, [&str, player] {
        debugLog("translateStringWithPlayer: FIRST CALL, str=\"%.200s\", player=%p, fn=%p",
                 str.c_str(), player, getDllFuncs() ? getDllFuncs()->translateStringWithPlayer : nullptr);
    });
    return callStringFuncPlayer(f->translateStringWithPlayer, str.c_str(), player);
}

std::string PlaceholderApi::translateStringWithPlayer(std::string const& str, Player* player, bool skipRemote) {
    auto* f = getDllFuncs();
    if (!f) return str;
    return callStringFuncPlayerSkip(f->translateStringWithPlayerSkip, str.c_str(), player, skipRemote);
}

std::string PlaceholderApi::getValue(std::string const& name) {
    auto* f = getDllFuncs();
    if (!f) return "";
    return callStringFunc(f->getValue, name.c_str());
}

std::string PlaceholderApi::getValueWithPlayer(std::string const& name, Player* player) {
    auto* f = getDllFuncs();
    if (!f) return "";
    return callStringFuncPlayer(f->getValueWithPlayer, name.c_str(), player);
}

bool PlaceholderApi::hasPlaceholder(std::string const& name) {
    auto* f = getDllFuncs();
    if (!f) return false;
    return f->hasPlaceholder(name.c_str()) != 0;
}

bool PlaceholderApi::registerServerPlaceholder(
    std::string const& pluginName, std::string const& name, PlaceholderCallback cb
) {
    auto* f = getDllFuncs();
    if (!f) return false;
    // 将 std::function 存储在静态库侧，分配 callbackId
    uint64_t callbackId = DllLoader::getInstance().registerCallback(std::move(cb));
    // type=0 (Server), updateIntervalMs=0
    return f->registerPlaceholder(pluginName.c_str(), name.c_str(), 0, callbackId, 0) != 0;
}

bool PlaceholderApi::registerPlayerPlaceholder(
    std::string const& pluginName, std::string const& name, PlaceholderCallback cb
) {
    auto* f = getDllFuncs();
    if (!f) return false;
    uint64_t callbackId = DllLoader::getInstance().registerCallback(std::move(cb));
    // type=1 (Player), updateIntervalMs=0
    return f->registerPlaceholder(pluginName.c_str(), name.c_str(), 1, callbackId, 0) != 0;
}

bool PlaceholderApi::registerStaticPlaceholder(
    std::string const& pluginName, std::string const& name, PlaceholderCallback cb, int updateIntervalMs
) {
    auto* f = getDllFuncs();
    if (!f) return false;
    uint64_t callbackId = DllLoader::getInstance().registerCallback(std::move(cb));
    // type=2 (Static)
    return f->registerPlaceholder(pluginName.c_str(), name.c_str(), 2, callbackId, updateIntervalMs) != 0;
}

bool PlaceholderApi::registerServerPlaceholderWithParams(
    std::string const& pluginName, std::string const& name, PlaceholderParamCallback cb
) {
    auto* f = getDllFuncs();
    if (!f) return false;
    // 旧版 DLL 无带参导出（可选解析失败），直接失败返回
    if (!f->registerPlaceholderWithParams) return false;
    uint64_t callbackId = DllLoader::getInstance().registerCallbackWithParams(std::move(cb));
    // type=0 (Server), updateIntervalMs=0
    return f->registerPlaceholderWithParams(pluginName.c_str(), name.c_str(), 0, callbackId, 0) != 0;
}

bool PlaceholderApi::registerPlayerPlaceholderWithParams(
    std::string const& pluginName, std::string const& name, PlaceholderParamCallback cb
) {
    auto* f = getDllFuncs();
    if (!f) return false;
    if (!f->registerPlaceholderWithParams) return false;
    uint64_t callbackId = DllLoader::getInstance().registerCallbackWithParams(std::move(cb));
    // type=1 (Player), updateIntervalMs=0
    return f->registerPlaceholderWithParams(pluginName.c_str(), name.c_str(), 1, callbackId, 0) != 0;
}

uint32_t PlaceholderApi::getAbiVersion() {
    // wrapper 侧：已加载 DLL 运行时报告的 ABI 版本（未加载/旧版 DLL 返回 0）
    return DllLoader::getInstance().loadedAbiVersion();
}

bool PlaceholderApi::isParamPapiSupported() {
    // wrapper 侧：查询已加载 DLL 的功能位（含防御性兜底）
    return DllLoader::getInstance().paramPapiSupported();
}

} // namespace meowpapi
