// DllLoader.cpp - MeowPAPI.dll 加载器实现（静态库侧）
//
// 静态库将 MeowPAPI.dll 二进制数据嵌入消费者插件内部。
// 首次调用 load() 时：
//   1. 检测 MeowPAPI.dll 是否已加载（LeviLamina 已作为插件加载）
//   2. 若未加载，尝试从磁盘加载（之前部署过但 LoadLibraryW 未成功）
//   3. 若磁盘上没有，从嵌入数据释放 DLL 和 manifest.json 到 plugins/MeowPAPI/
//   4. LoadLibraryW 加载 DLL
//   5. GetProcAddress 解析所有导出函数
//   6. 设置回调调用器
//   7. 自动初始化（initAsServer + registerBuiltinPlaceholders + exportRemoteCallApi + installBepApiFallback）
//
// 多消费者插件防冲突：std::mutex 保证部署只执行一次。
// 第一个加载的消费者插件部署并初始化 MeowPAPI，后续插件直接连接。
//
// 修复：原使用 std::call_once 导致首次加载失败后无法重试。
// 现使用 mutex + 重试逻辑，允许后续调用重试 LoadLibraryW。
//
// 早期部署：通过后台线程在消费者插件 DllMain 返回后立即触发 load()，
// 使 DLL 在 enable() 被调用之前就已完成部署和加载。
#include "DllLoader.h"

#include <Windows.h>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

// RemoteCall 代理函数指针类型定义（用于解析 RC_* 导出函数）
#include "meowpapi/RemoteCallAPI.h"

namespace fs = std::filesystem;

namespace meowpapi {

namespace {

constexpr wchar_t const* DLL_NAME     = L"MeowPAPI.dll";
constexpr wchar_t const* DLL_DIR_NAME = L"MeowPAPI";

// 诊断日志：写入文件 meowpapi_loader_debug.log
void debugLog(char const* fmt, ...) {
    try {
        char buf[2048];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        auto now    = std::chrono::system_clock::now();
        auto t      = std::chrono::system_clock::to_time_t(now);
        auto ms     = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        struct tm   tmv;
        localtime_s(&tmv, &t);

        char line[2300];
        int prefixLen = snprintf(line, sizeof(line),
            "[%04d-%02d-%02d %02d:%02d:%02d.%03d][DllLoader] ",
            tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
            tmv.tm_hour, tmv.tm_min, tmv.tm_sec, (int)ms.count());
        snprintf(line + prefixLen, sizeof(line) - prefixLen, "%s\n", buf);

        // 追加写入到服务器根目录
        static std::mutex logMtx;
        std::lock_guard<std::mutex> lock(logMtx);
        std::ofstream f("meowpapi_loader_debug.log", std::ios::app);
        if (f.is_open()) {
            f.write(line, strlen(line));
        }
    } catch (...) {}
}

// 从 HMODULE 获取函数指针
template <typename T>
T getFunc(HMODULE h, const char* name) {
    return reinterpret_cast<T>(GetProcAddress(h, name));
}

// 读取磁盘 DLL 文件的 PE TimeDateStamp（不加载 DLL）
// PE 格式: DOS header → e_lfanew@0x3C → PE sig(4B) + COFF: Machine(2)+NumSections(2)+TimeDateStamp(4)
uint32_t readFileTimestamp(std::wstring const& dllPath) {
    std::ifstream f(dllPath, std::ios::binary);
    if (!f.is_open()) return 0;

    uint32_t e_lfanew = 0;
    f.seekg(0x3C, std::ios::beg);
    f.read(reinterpret_cast<char*>(&e_lfanew), 4);
    if (!f || e_lfanew == 0 || e_lfanew > 0x10000000) return 0;

    uint32_t timestamp = 0;
    f.seekg(e_lfanew + 8, std::ios::beg); // PE sig(4) + Machine(2) + NumSections(2)
    f.read(reinterpret_cast<char*>(&timestamp), 4);
    return timestamp;
}

// 读取已加载模块的 PE TimeDateStamp
uint32_t getModuleTimestamp(HMODULE hMod) {
    if (!hMod) return 0;
    auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(hMod);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    auto* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(
        reinterpret_cast<uint8_t*>(hMod) + dosHeader->e_lfanew
    );
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return 0;
    return ntHeaders->FileHeader.TimeDateStamp;
}

// 写入二进制数据到文件（先写临时文件再重命名，避免文件锁冲突）
bool writeFile(fs::path const& outPath, const uint8_t* data, size_t size) {
    auto tmpPath = outPath;
    tmpPath += L".tmp";

    {
        std::ofstream out(tmpPath, std::ios::out | std::ios::binary);
        if (!out.is_open()) return false;
        out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }

    std::error_code ec;
    if (fs::exists(outPath, ec)) {
        fs::rename(tmpPath, outPath, ec);
        if (ec) {
            ec.clear();
            fs::remove(outPath, ec);
            ec.clear();
            fs::rename(tmpPath, outPath, ec);
        }
    } else {
        fs::rename(tmpPath, outPath, ec);
    }

    if (ec) {
        fs::copy_file(tmpPath, outPath, fs::copy_options::overwrite_existing, ec);
        if (!ec) {
            fs::remove(tmpPath, ec);
            ec.clear();
        }
    }
    return !ec;
}

// 早期部署器：在消费者插件 DLL 加载时创建后台线程，
// 线程在 DllMain 返回后立即触发 load()，提前部署和加载 MeowPAPI.dll。
// 这避免了 enable() 阶段才首次部署导致的延迟。
struct EarlyDeployer {
    EarlyDeployer() {
        CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            Sleep(100); // 等待 DllMain 返回，避免 loader lock 死锁
            debugLog("EarlyDeployer: calling load(skipAutoInit=true)");
            bool ok = DllLoader::getInstance().load(true); // skipAutoInit=true，仅部署和加载
            debugLog("EarlyDeployer: load returned %s", ok ? "true" : "false");
            return 0;
        }, nullptr, 0, nullptr);
    }
};
// 静态初始化器：每个消费者插件 DLL 加载时都会执行
static EarlyDeployer g_earlyDeployer;

} // anonymous namespace

DllLoader& DllLoader::getInstance() {
    static DllLoader instance;
    return instance;
}

bool DllLoader::load(bool skipAutoInit) {
    if (mLoaded) {
        // DLL 已加载，若需要 autoInit 且尚未执行，则补执行
        if (!skipAutoInit && mDeployed && !mAutoInitialized) {
            autoInit();
        }
        return true;
    }

    static std::mutex mtx;
    std::lock_guard<std::mutex> lock(mtx);

    // 双重检查：可能在等锁期间已被其他线程加载
    if (mLoaded) {
        if (!skipAutoInit && mDeployed && !mAutoInitialized) {
            autoInit();
        }
        return true;
    }

    debugLog("load(skipAutoInit=%d): starting, mLoaded=%d mDeployed=%d mAutoInit=%d",
             skipAutoInit ? 1 : 0, mLoaded ? 1 : 0, mDeployed ? 1 : 0, mAutoInitialized ? 1 : 0);

    uint32_t embTs = getEmbeddedBuildTimestamp();
    debugLog("load: embedded build timestamp = %u", embTs);

    // 1. 检测 MeowPAPI.dll 是否已加载（LeviLamina 或其他消费者插件已处理）
    HMODULE hMod = GetModuleHandleW(DLL_NAME);
    debugLog("load: GetModuleHandleW(\"MeowPAPI.dll\") = %p", hMod);

    if (hMod) {
        // 已加载 — 检查版本，若过期则更新磁盘文件（当前会话仍用旧版本，下次重启生效）
        uint32_t modTs = getModuleTimestamp(hMod);
        debugLog("load: loaded module timestamp = %u", modTs);
        if (embTs > 0 && modTs > 0 && modTs < embTs) {
            debugLog("load: LOADED MeowPAPI is OLDER (loaded=%u, embedded=%u), updating file for next restart",
                     modTs, embTs);
            updateFileFromEmbedded();
        }
    } else {
        // 未加载 — 检查磁盘文件版本
        std::wstring pluginsDir = findPluginsDir();
        debugLog("load: pluginsDir = \"%S\"", pluginsDir.c_str());
        if (!pluginsDir.empty()) {
            std::wstring dllPath = pluginsDir + L"\\" + DLL_DIR_NAME + L"\\" + DLL_NAME;
            bool exists = fs::exists(dllPath);
            debugLog("load: disk DLL path = \"%S\", exists = %d", dllPath.c_str(), exists ? 1 : 0);
            if (exists) {
                // 版本检查：比较磁盘文件时间戳与嵌入时间戳
                uint32_t diskTs = readFileTimestamp(dllPath);
                debugLog("load: disk file timestamp = %u", diskTs);
                if (embTs > 0 && (diskTs == 0 || diskTs < embTs)) {
                    debugLog("load: DISK MeowPAPI is OLDER (disk=%u, embedded=%u), replacing file",
                             diskTs, embTs);
                    updateFileFromEmbedded();
                }
                // 加载（可能是刚更新的）磁盘文件
                hMod = LoadLibraryW(dllPath.c_str());
                debugLog("load: LoadLibraryW returned %p (err=%lu)", hMod, GetLastError());
                if (hMod) mDeployed = true;
            }
        }

        // 3. 若磁盘上没有（或加载失败），从嵌入数据部署
        if (!hMod) {
            static bool deployAttempted = false;
            if (!deployAttempted) {
                deployAttempted = true;
                debugLog("load: attempting deployFromEmbedded()");
                hMod = static_cast<HMODULE>(deployFromEmbedded());
                debugLog("load: deployFromEmbedded returned %p", hMod);
                // deployFromEmbedded 内部设置 mDeployed
            } else {
                debugLog("load: deployFromEmbedded already attempted, skipping");
            }
        }
    }

    if (!hMod) {
        debugLog("load: FAILED - no module handle obtained");
        return false;
    }
    mModule = hMod;
    debugLog("load: mModule = %p, calling resolveFunctions()", mModule);

    // 4. 解析所有函数指针
    if (!resolveFunctions()) {
        debugLog("load: resolveFunctions() FAILED");
        mModule = nullptr;
        return false;
    }
    debugLog("load: resolveFunctions() succeeded");

    // 5. 设置回调调用器
    mFuncs.setCallbackInvoker(&DllLoader::callbackInvoker);
    debugLog("load: setCallbackInvoker done");
    // 带参回调调用器（旧版 DLL 无此导出时为 nullptr，跳过设置，带参功能降级）
    if (mFuncs.setCallbackInvokerWithParams) {
        mFuncs.setCallbackInvokerWithParams(&DllLoader::callbackInvokerWithParams);
        debugLog("load: setCallbackInvokerWithParams done");
    } else {
        debugLog("load: setCallbackInvokerWithParams NOT available (old DLL), param PAPI degraded");
    }

    mLoaded = true;
    debugLog("load: mLoaded = true, mDeployed = %d, skipAutoInit = %d", mDeployed ? 1 : 0, skipAutoInit ? 1 : 0);
    // ABI 诊断：运行时 DLL 版本与功能位（0 = 旧版 DLL 无 ABI 导出）
    debugLog("load: DLL ABI = 0x%06X, features = 0x%08X, paramPapi = %d",
        loadedAbiVersion(), loadedAbiFeatures(), paramPapiSupported() ? 1 : 0);

    // 6. 自动初始化（仅在本实例部署了 DLL 时执行）
    //    若 DLL 已由 LeviLamina 作为插件加载，其 PluginEntry 已处理初始化
    if (!skipAutoInit && mDeployed) {
        autoInit();
    }

    return true;
}

void* DllLoader::deployFromEmbedded() {
    // 释放文件到磁盘（复用 updateFileFromEmbedded）
    if (!updateFileFromEmbedded()) return nullptr;

    // LoadLibraryW 加载 DLL
    std::wstring pluginsDir = findPluginsDir();
    if (pluginsDir.empty()) return nullptr;
    std::wstring dllPath = pluginsDir + L"\\" + DLL_DIR_NAME + L"\\" + DLL_NAME;

    HMODULE hMod = LoadLibraryW(dllPath.c_str());
    if (!hMod) return nullptr;

    mDeployed = true;
    return hMod;
}

bool DllLoader::updateFileFromEmbedded() {
    // 1. 查找 plugins/ 目录
    std::wstring pluginsDir = findPluginsDir();
    if (pluginsDir.empty()) return false;

    // 2. 创建 plugins/MeowPAPI/ 目录
    std::wstring meowpapiDir = pluginsDir + L"\\" + DLL_DIR_NAME;
    std::error_code ec;
    fs::create_directories(meowpapiDir, ec);
    if (ec && !fs::is_directory(meowpapiDir)) return false;

    // 3. 释放 MeowPAPI.dll（覆盖旧文件）
    std::wstring dllPath = meowpapiDir + L"\\" + DLL_NAME;
    auto* dllData = getEmbeddedDllData();
    auto  dllSize = getEmbeddedDllSize();
    if (!dllData || dllSize == 0) return false;
    if (!writeFile(fs::path(dllPath), dllData, dllSize)) return false;

    // 4. 释放 manifest.json
    std::wstring manifestPath = meowpapiDir + L"\\manifest.json";
    auto* mfData = getEmbeddedManifestData();
    auto  mfSize = getEmbeddedManifestSize();
    if (mfData && mfSize > 0) {
        writeFile(fs::path(manifestPath), mfData, mfSize);
    }

    debugLog("updateFileFromEmbedded: file updated (DLL=%zu bytes, manifest=%zu bytes, timestamp=%u)",
             dllSize, mfSize, getEmbeddedBuildTimestamp());
    return true;
}

bool DllLoader::checkAndUpdateVersion(void* hLoaded) {
    uint32_t embTs = getEmbeddedBuildTimestamp();
    if (embTs == 0) {
        debugLog("checkAndUpdateVersion: embedded timestamp is 0, skipping");
        return false;
    }

    HMODULE hMod = static_cast<HMODULE>(hLoaded);
    if (hMod) {
        // DLL 已加载 — 检查模块时间戳
        uint32_t modTs = getModuleTimestamp(hMod);
        if (modTs > 0 && modTs < embTs) {
            debugLog("checkAndUpdateVersion: LOADED MeowPAPI is OLDER (loaded=%u, embedded=%u), "
                     "updating file for next restart (current session uses old version)", modTs, embTs);
            updateFileFromEmbedded();
            return true;
        }
    } else {
        // DLL 未加载 — 检查磁盘文件时间戳
        std::wstring pluginsDir = findPluginsDir();
        if (pluginsDir.empty()) return false;
        std::wstring dllPath = pluginsDir + L"\\" + DLL_DIR_NAME + L"\\" + DLL_NAME;
        if (!fs::exists(dllPath)) return false;

        uint32_t diskTs = readFileTimestamp(dllPath);
        if (diskTs == 0 || diskTs < embTs) {
            debugLog("checkAndUpdateVersion: DISK MeowPAPI is OLDER (disk=%u, embedded=%u), replacing",
                     diskTs, embTs);
            updateFileFromEmbedded();
            return true;
        }
    }
    return false;
}

std::wstring DllLoader::findPluginsDir() const {
    // 从当前模块（消费者插件 DLL）位置上溯到 plugins/ 目录
    // ...\plugins\ConsumerPlugin\ConsumerPlugin.dll -> ...\plugins
    HMODULE hThisModule = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&getInstance),
            &hThisModule) || !hThisModule) {
        return {};
    }

    wchar_t modulePath[MAX_PATH];
    if (!GetModuleFileNameW(hThisModule, modulePath, MAX_PATH)) return {};

    std::wstring path(modulePath);
    // ...\plugins\ConsumerPlugin\ConsumerPlugin.dll
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return {};
    std::wstring pluginDir = path.substr(0, pos); // ...\plugins\ConsumerPlugin

    pos = pluginDir.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return {};
    return pluginDir.substr(0, pos); // ...\plugins
}

void DllLoader::autoInit() {
    if (mAutoInitialized) return;
    mAutoInitialized = true;

    // 按顺序初始化 PAPI 服务端
    if (mFuncs.initAsServer) mFuncs.initAsServer();
    if (mFuncs.registerBuiltinPlaceholders) mFuncs.registerBuiltinPlaceholders();
    if (mFuncs.exportRemoteCallApi) mFuncs.exportRemoteCallApi();
    if (mFuncs.installBepApiFallback) mFuncs.installBepApiFallback();
}

bool DllLoader::resolveFunctions() {
    auto h = reinterpret_cast<HMODULE>(mModule);
    if (!h) return false;

    #define RESOLVE(field, exportName, type) \
        mFuncs.field = getFunc<type>(h, exportName); \
        if (!mFuncs.field) { \
            debugLog("resolveFunctions: FAILED to resolve %s", exportName); \
            return false; \
        } else { \
            debugLog("resolveFunctions: OK %s = %p", exportName, mFuncs.field); \
        }

    RESOLVE(getVersion,                   "MeowPAPI_GetVersion",                   const char* (*)());
    RESOLVE(getBuildTimestamp,            "MeowPAPI_GetBuildTimestamp",            uint64_t (*)());
    RESOLVE(initAsServer,                 "MeowPAPI_InitAsServer",                 void (*)());
    RESOLVE(initAsClient,                 "MeowPAPI_InitAsClient",                 void (*)());
    RESOLVE(setCallbackInvoker,           "MeowPAPI_SetCallbackInvoker",           void (*)(MeowPAPI_CallbackFn));
    RESOLVE(translateString,              "MeowPAPI_TranslateString",              int (*)(const char*, char*, int));
    RESOLVE(translateStringWithPlayer,    "MeowPAPI_TranslateStringWithPlayer",    int (*)(const char*, void*, char*, int));
    RESOLVE(translateStringWithPlayerSkip,"MeowPAPI_TranslateStringWithPlayerSkip",int (*)(const char*, void*, int, char*, int));
    RESOLVE(getValue,                     "MeowPAPI_GetValue",                     int (*)(const char*, char*, int));
    RESOLVE(getValueWithPlayer,           "MeowPAPI_GetValueWithPlayer",           int (*)(const char*, void*, char*, int));
    RESOLVE(hasPlaceholder,               "MeowPAPI_HasPlaceholder",               int (*)(const char*));
    RESOLVE(registerPlaceholder,          "MeowPAPI_RegisterPlaceholder",          int (*)(const char*, const char*, int, uint64_t, int));
    RESOLVE(unregisterPlaceholder,        "MeowPAPI_UnregisterPlaceholder",        int (*)(const char*));
    RESOLVE(unregisterByPlugin,           "MeowPAPI_UnregisterByPlugin",           void (*)(const char*));
    RESOLVE(clear,                        "MeowPAPI_Clear",                        void (*)());
    RESOLVE(listPlaceholders,             "MeowPAPI_ListPlaceholders",             int (*)(char*, int));
    RESOLVE(listPlaceholdersByPlugin,     "MeowPAPI_ListPlaceholdersByPlugin",     int (*)(const char*, char*, int));
    RESOLVE(exportRemoteCallApi,          "MeowPAPI_ExportRemoteCallApi",          void (*)());
    RESOLVE(removeRemoteCallApi,          "MeowPAPI_RemoveRemoteCallApi",          void (*)());
    RESOLVE(registerBuiltinPlaceholders,  "MeowPAPI_RegisterBuiltinPlaceholders",  void (*)());
    RESOLVE(isBepApiAvailable,            "MeowPAPI_IsBepApiAvailable",            int (*)());
    RESOLVE(installBepApiFallback,        "MeowPAPI_InstallBepApiFallback",        void (*)());
    RESOLVE(removeBepApiFallback,         "MeowPAPI_RemoveBepApiFallback",         void (*)());
    RESOLVE(isRemoteAvailable,            "MeowPAPI_IsRemoteAvailable",            int (*)());
    RESOLVE(remoteTranslateString,        "MeowPAPI_RemoteTranslateString",        int (*)(const char*, char*, int));
    RESOLVE(remoteTranslateStringWithPlayer,"MeowPAPI_RemoteTranslateStringWithPlayer",int (*)(const char*, const char*, char*, int));
    RESOLVE(remoteGetValue,               "MeowPAPI_RemoteGetValue",               int (*)(const char*, char*, int));
    RESOLVE(remoteGetValueWithPlayer,     "MeowPAPI_RemoteGetValueWithPlayer",     int (*)(const char*, const char*, char*, int));
    RESOLVE(remoteHasPlaceholder,         "MeowPAPI_RemoteHasPlaceholder",         int (*)(const char*));

    #undef RESOLVE

    // 带参扩展（GMLIB PAPI 兼容）：可选解析——旧版已加载 DLL 缺少这些导出时不失败，
    // 仅带参功能降级不可用（磁盘文件会被嵌入版本覆盖，下次重启升级）
    #define RESOLVE_OPT(field, exportName, type) \
        mFuncs.field = getFunc<type>(h, exportName); \
        if (!mFuncs.field) { \
            debugLog("resolveFunctions: OPTIONAL %s not found (old DLL, param PAPI degraded)", exportName); \
        } else { \
            debugLog("resolveFunctions: OK %s = %p", exportName, mFuncs.field); \
        }

    RESOLVE_OPT(setCallbackInvokerWithParams, "MeowPAPI_SetCallbackInvokerWithParams",
                void (*)(MeowPAPI_CallbackWithParamsFn));
    RESOLVE_OPT(registerPlaceholderWithParams, "MeowPAPI_RegisterPlaceholderWithParams",
                int (*)(const char*, const char*, int, uint64_t, int));
    // ABI 查询接口（旧 DLL 无此导出时返回 0，消费者据此降级）
    RESOLVE_OPT(getAbiVersion, "MeowPAPI_GetAbiVersion", uint32_t (*)());
    RESOLVE_OPT(getAbiFeatures, "MeowPAPI_GetAbiFeatures", uint32_t (*)());

    #undef RESOLVE_OPT

    // 解析 RemoteCall 代理函数（RC_* 系列）
    // 这些函数在 RemoteCallProxy.cpp 中以 extern "C" 导出，名称不修饰。
    // 若解析失败，消费者插件的 exportAs/hasFunc 将全部静默返回 false。
    #define RC_RESOLVE(field, name, type) \
        RemoteCall::detail::field = getFunc<type>(h, name); \
        if (!RemoteCall::detail::field) { \
            debugLog("resolveFunctions: FAILED to resolve %s (consumer RemoteCall will not work!)", name); \
        } else { \
            debugLog("resolveFunctions: OK %s = %p", name, RemoteCall::detail::field); \
        }

    RC_RESOLVE(p_exportFunc,      "RC_ExportFunc",      RemoteCall::detail::ExportFuncFn);
    RC_RESOLVE(p_importFunc,      "RC_ImportFunc",      RemoteCall::detail::ImportFuncFn);
    RC_RESOLVE(p_hasFunc,         "RC_HasFunc",         RemoteCall::detail::HasFuncFn);
    RC_RESOLVE(p_removeFunc,      "RC_RemoveFunc",      RemoteCall::detail::RemoveFuncFn);
    RC_RESOLVE(p_removeNameSpace, "RC_RemoveNameSpace", RemoteCall::detail::RemoveNameSpaceFn);
    RC_RESOLVE(p_removeFuncs,     "RC_RemoveFuncs",     RemoteCall::detail::RemoveFuncsFn);
    RC_RESOLVE(p_onCallError,     "RC_OnCallError",     RemoteCall::detail::OnCallErrorFn);

    #undef RC_RESOLVE

    return true;
}

DllFunctions const* DllLoader::functions() const {
    if (!mLoaded) return nullptr;
    return &mFuncs;
}

uint32_t DllLoader::loadedAbiVersion() const {
    if (!mLoaded || !mFuncs.getAbiVersion) return 0;
    return mFuncs.getAbiVersion();
}

uint32_t DllLoader::loadedAbiFeatures() const {
    if (!mLoaded || !mFuncs.getAbiFeatures) return 0;
    return mFuncs.getAbiFeatures();
}

bool DllLoader::paramPapiSupported() const {
    // 双重判定：ABI 功能位（新版 DLL）或带参注册导出存在（防御性兜底）
    if (loadedAbiFeatures() & MEOWPAPI_ABI_FEATURE_PARAMS) return true;
    return mLoaded && mFuncs.registerPlaceholderWithParams != nullptr;
}

uint64_t DllLoader::registerCallback(std::function<std::string(Player*)> cb) {
    uint64_t id = mNextCallbackId++;
    mCallbacks.emplace(id, std::move(cb));
    return id;
}

uint64_t DllLoader::registerCallbackWithParams(std::function<std::string(Player*, std::string const&)> cb) {
    uint64_t id = mNextCallbackId++;
    mCallbacksWithParams.emplace(id, std::move(cb));
    return id;
}

void DllLoader::unregisterCallback(uint64_t id) {
    mCallbacks.erase(id);
    mCallbacksWithParams.erase(id);
}

void DllLoader::callbackInvoker(uint64_t callbackId, void* player, char* out, int outSize) {
    auto& loader = getInstance();
    auto it = loader.mCallbacks.find(callbackId);
    if (it == loader.mCallbacks.end()) {
        if (out && outSize > 0) out[0] = '\0';
        return;
    }
    try {
        std::string result = it->second(reinterpret_cast<Player*>(player));
        int len = static_cast<int>(result.size());
        int copyLen = (len < outSize - 1) ? len : (outSize - 1);
        std::memcpy(out, result.data(), copyLen);
        out[copyLen] = '\0';
    } catch (...) {
        if (out && outSize > 0) {
            const char* err = "{ERR}";
            std::memcpy(out, err, 5);
            out[5] = '\0';
        }
    }
}

void DllLoader::callbackInvokerWithParams(
    uint64_t callbackId, void* player, const char* paramsJson, char* out, int outSize
) {
    auto& loader = getInstance();
    auto it = loader.mCallbacksWithParams.find(callbackId);
    if (it == loader.mCallbacksWithParams.end()) {
        if (out && outSize > 0) out[0] = '\0';
        return;
    }
    try {
        std::string result = it->second(
            reinterpret_cast<Player*>(player),
            paramsJson ? paramsJson : "{}"
        );
        int len = static_cast<int>(result.size());
        int copyLen = (len < outSize - 1) ? len : (outSize - 1);
        std::memcpy(out, result.data(), copyLen);
        out[copyLen] = '\0';
    } catch (...) {
        if (out && outSize > 0) {
            const char* err = "{ERR}";
            std::memcpy(out, err, 5);
            out[5] = '\0';
        }
    }
}

} // namespace meowpapi
