// DllExports.cpp - MeowPAPI.dll 导出函数实现
// 将内部 C++ 类（PlaceholderApi, PlaceholderRegistry, Builtins, RemoteCallBridge, BepApiBridge）
// 包装为 C 接口导出，供静态库包装层通过 LoadLibrary + GetProcAddress 调用。
//
// 注意：MEOWPAPI_DLL_EXPORTS 宏由 xmake.lua 的 add_defines 在 target 级别定义，
// 此处不再重复 #define，避免 C4005 宏重定义警告。
// 该宏使 meowpapi/RemoteCallAPI.h 走 DLL 模式（__declspec(dllimport) 从 lrca 导入）。
#include "meowpapi/DllExports.h"

#include "meowpapi/PlaceholderApi.h"
#include "meowpapi/PlaceholderRegistry.h"
#include "meowpapi/Builtins.h"
#include "meowpapi/RemoteCallBridge.h"
#include "meowpapi/BepApiBridge.h"

#include <cstring>
#include <nlohmann/json.hpp>
#include <string>
#include <Windows.h>

namespace {

// 辅助：将 std::string 写入输出缓冲区，返回字符串长度
int writeResult(std::string const& result, char* out, int outSize) {
    int len = static_cast<int>(result.size());
    if (!out || outSize <= 0) return len;
    int copyLen = (len < outSize - 1) ? len : (outSize - 1);
    std::memcpy(out, result.data(), copyLen);
    out[copyLen] = '\0';
    return len;
}

} // anonymous namespace

extern "C" {

//===== 版本 =====
MEOWPAPI_API const char* MeowPAPI_GetVersion(void) {
    return "MeowPAPI " MEOWPAPI_VERSION_STRING;
}

MEOWPAPI_API uint32_t MeowPAPI_GetAbiVersion(void) {
    return MEOWPAPI_ABI_VERSION;
}

MEOWPAPI_API uint32_t MeowPAPI_GetAbiFeatures(void) {
    uint32_t features = 0;
    features |= MEOWPAPI_ABI_FEATURE_PARAMS; // 带参占位符（GMLIB PAPI 兼容）
    return features;
}

MEOWPAPI_API uint64_t MeowPAPI_GetBuildTimestamp(void) {
    // 读取本模块的 PE TimeDateStamp（链接器在构建时设置的 Unix 时间戳）
    HMODULE hThis = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&MeowPAPI_GetBuildTimestamp),
        &hThis
    );
    if (!hThis) return 0;
    auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(hThis);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    auto* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(
        reinterpret_cast<uint8_t*>(hThis) + dosHeader->e_lfanew
    );
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return 0;
    return ntHeaders->FileHeader.TimeDateStamp;
}

//===== 初始化 =====
MEOWPAPI_API void MeowPAPI_InitAsServer(void) {
    meowpapi::PlaceholderApi::getInstance().initAsServer();
}

MEOWPAPI_API void MeowPAPI_InitAsClient(void) {
    meowpapi::PlaceholderApi::getInstance().initAsClient();
}

//===== 回调注册 =====
MEOWPAPI_API void MeowPAPI_SetCallbackInvoker(MeowPAPI_CallbackFn fn) {
    meowpapi::PlaceholderRegistry::getInstance().setCallbackInvoker(
        reinterpret_cast<meowpapi::PlaceholderRegistry::CallbackInvoker>(fn)
    );
}

MEOWPAPI_API void MeowPAPI_SetCallbackInvokerWithParams(MeowPAPI_CallbackWithParamsFn fn) {
    meowpapi::PlaceholderRegistry::getInstance().setCallbackInvokerWithParams(
        reinterpret_cast<meowpapi::PlaceholderRegistry::CallbackInvokerWithParams>(fn)
    );
}

//===== 翻译 =====
MEOWPAPI_API int MeowPAPI_TranslateString(const char* str, char* out, int outSize) {
    auto result = meowpapi::PlaceholderApi::getInstance().translateString(str ? str : "");
    return writeResult(result, out, outSize);
}

MEOWPAPI_API int MeowPAPI_TranslateStringWithPlayer(const char* str, void* player, char* out, int outSize) {
    auto result = meowpapi::PlaceholderApi::getInstance().translateStringWithPlayer(
        str ? str : "", reinterpret_cast<Player*>(player)
    );
    return writeResult(result, out, outSize);
}

MEOWPAPI_API int MeowPAPI_TranslateStringWithPlayerSkip(const char* str, void* player, int skipRemote, char* out, int outSize) {
    auto result = meowpapi::PlaceholderApi::getInstance().translateStringWithPlayer(
        str ? str : "", reinterpret_cast<Player*>(player), skipRemote != 0
    );
    return writeResult(result, out, outSize);
}

//===== 获取值 =====
MEOWPAPI_API int MeowPAPI_GetValue(const char* name, char* out, int outSize) {
    auto result = meowpapi::PlaceholderApi::getInstance().getValue(name ? name : "");
    return writeResult(result, out, outSize);
}

MEOWPAPI_API int MeowPAPI_GetValueWithPlayer(const char* name, void* player, char* out, int outSize) {
    auto result = meowpapi::PlaceholderApi::getInstance().getValueWithPlayer(
        name ? name : "", reinterpret_cast<Player*>(player)
    );
    return writeResult(result, out, outSize);
}

//===== 检查 =====
MEOWPAPI_API int MeowPAPI_HasPlaceholder(const char* name) {
    return meowpapi::PlaceholderApi::getInstance().hasPlaceholder(name ? name : "") ? 1 : 0;
}

//===== 注册占位符 =====
MEOWPAPI_API int MeowPAPI_RegisterPlaceholder(
    const char* pluginName, const char* name, int type,
    uint64_t callbackId, int updateIntervalMs
) {
    // 外部回调始终标记为 mainThreadOnly=true（安全第一）
    bool ok = meowpapi::PlaceholderRegistry::getInstance().registerPlaceholderWithCallbackId(
        pluginName ? pluginName : "",
        name ? name : "",
        static_cast<meowpapi::PlaceholderType>(type),
        callbackId,
        updateIntervalMs,
        true // mainThreadOnly
    );
    return ok ? 1 : 0;
}

MEOWPAPI_API int MeowPAPI_RegisterPlaceholderWithParams(
    const char* pluginName, const char* name, int type,
    uint64_t callbackId, int updateIntervalMs
) {
    // 外部回调始终标记为 mainThreadOnly=true（安全第一）
    bool ok = meowpapi::PlaceholderRegistry::getInstance().registerPlaceholderWithParamsAndCallbackId(
        pluginName ? pluginName : "",
        name ? name : "",
        static_cast<meowpapi::PlaceholderType>(type),
        callbackId,
        updateIntervalMs,
        true // mainThreadOnly
    );
    return ok ? 1 : 0;
}

MEOWPAPI_API int MeowPAPI_UnregisterPlaceholder(const char* name) {
    return meowpapi::PlaceholderRegistry::getInstance().unregisterPlaceholder(name ? name : "") ? 1 : 0;
}

MEOWPAPI_API void MeowPAPI_UnregisterByPlugin(const char* pluginName) {
    meowpapi::PlaceholderRegistry::getInstance().unregisterByPlugin(pluginName ? pluginName : "");
}

MEOWPAPI_API void MeowPAPI_Clear(void) {
    meowpapi::PlaceholderRegistry::getInstance().clear();
}

//===== 列表 =====
MEOWPAPI_API int MeowPAPI_ListPlaceholders(char* out, int outSize) {
    auto list = meowpapi::PlaceholderRegistry::getInstance().listPlaceholders();
    nlohmann::json j = nlohmann::json::array();
    for (auto const& n : list) j.push_back(n);
    return writeResult(j.dump(), out, outSize);
}

MEOWPAPI_API int MeowPAPI_ListPlaceholdersByPlugin(const char* pluginName, char* out, int outSize) {
    auto list = meowpapi::PlaceholderRegistry::getInstance().listPlaceholdersByPlugin(pluginName ? pluginName : "");
    nlohmann::json j = nlohmann::json::array();
    for (auto const& n : list) j.push_back(n);
    return writeResult(j.dump(), out, outSize);
}

//===== 服务端：RemoteCall 导出 =====
MEOWPAPI_API void MeowPAPI_ExportRemoteCallApi(void) {
    // DLL 模式下 lrca 在 MeowPAPI.dll 加载前已就绪（硬依赖），直接导出
    meowpapi::exportRemoteCallApi();
}

MEOWPAPI_API void MeowPAPI_RemoveRemoteCallApi(void) {
    meowpapi::removeRemoteCallApi();
}

//===== 服务端：内置占位符 =====
MEOWPAPI_API void MeowPAPI_RegisterBuiltinPlaceholders(void) {
    meowpapi::registerBuiltinPlaceholders();
}

//===== 服务端：BEPAPI 兼容 =====
MEOWPAPI_API int MeowPAPI_IsBepApiAvailable(void) {
    return meowpapi::isBepApiAvailable() ? 1 : 0;
}

MEOWPAPI_API void MeowPAPI_InstallBepApiFallback(void) {
    meowpapi::installBepApiFallback();
}

MEOWPAPI_API void MeowPAPI_RemoveBepApiFallback(void) {
    meowpapi::removeBepApiFallback();
}

//===== 客户端：RemoteCall 桥接 =====
MEOWPAPI_API int MeowPAPI_IsRemoteAvailable(void) {
    return meowpapi::isRemoteAvailable() ? 1 : 0;
}

MEOWPAPI_API int MeowPAPI_RemoteTranslateString(const char* str, char* out, int outSize) {
    auto result = meowpapi::remoteTranslateString(str ? str : "");
    return writeResult(result, out, outSize);
}

MEOWPAPI_API int MeowPAPI_RemoteTranslateStringWithPlayer(const char* str, const char* playerName, char* out, int outSize) {
    auto result = meowpapi::remoteTranslateStringWithPlayerName(
        str ? str : "", playerName ? playerName : ""
    );
    return writeResult(result, out, outSize);
}

MEOWPAPI_API int MeowPAPI_RemoteGetValue(const char* name, char* out, int outSize) {
    auto result = meowpapi::remoteGetValue(name ? name : "");
    return writeResult(result, out, outSize);
}

MEOWPAPI_API int MeowPAPI_RemoteGetValueWithPlayer(const char* name, const char* playerName, char* out, int outSize) {
    auto result = meowpapi::remoteGetValueWithPlayerName(
        name ? name : "", playerName ? playerName : ""
    );
    return writeResult(result, out, outSize);
}

MEOWPAPI_API int MeowPAPI_RemoteHasPlaceholder(const char* name) {
    return meowpapi::remoteHasPlaceholder(name ? name : "") ? 1 : 0;
}

} // extern "C"
