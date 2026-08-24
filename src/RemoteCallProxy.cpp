// RemoteCallProxy.cpp - DLL 端 RemoteCall 代理实现
//
// 将 LegacyRemoteCall 的核心函数通过 MeowPAPI.dll 重新导出为 RC_* 接口，
// 使消费者插件无需直接链接 legacyremotecall。
//
// 本文件在 MeowPAPI_DLL target 中编译（定义 MEOWPAPI_DLL_EXPORTS），
// 因此 meowpapi/RemoteCallAPI.h 走 DLL 模式：exportFunc 等函数通过
// __declspec(dllimport) 直接从 LegacyRemoteCall.dll 导入。
// RC_* 导出函数内部直接调用 lrca 原始函数，无需运行时动态解析。
//
// 消费者插件侧（不定义 MEOWPAPI_DLL_EXPORTS）通过 DllLoader 解析 RC_*
// 到 p_* 函数指针，间接调用 lrca。
#include "meowpapi/RemoteCallAPI.h"

namespace meowpapi {

// 所有 RC_* 函数必须使用 extern "C" 导出，否则 C++ 名称修饰会导致
// 静态库 DllLoader 中 GetProcAddress(h, "RC_ExportFunc") 找不到函数，
// 进而使消费者插件的 RemoteCall::exportAs / hasFunc 全部静默失败。
// MSVC 下 extern "C" 仅影响名称修饰，不影响 C++ 参数传递。
extern "C" {

// 导出函数：注册 RemoteCall 回调
// 返回值：成功返回 true
__declspec(dllexport) bool RC_ExportFunc(
    std::string const& nameSpace,
    std::string const& funcName,
    RemoteCall::CallbackFn callback,
    void* handle
) {
    // DLL 模式下 exportFunc 是 __declspec(dllimport) 从 lrca 导入，直接调用
    return RemoteCall::exportFunc(nameSpace, funcName, std::move(callback), handle);
}

// 导出函数：导入 RemoteCall 回调
// 返回值：回调函数的 const 引用
__declspec(dllexport) RemoteCall::CallbackFn const& RC_ImportFunc(
    std::string const& nameSpace,
    std::string const& funcName
) {
    return RemoteCall::importFunc(nameSpace, funcName);
}

// 导出函数：检查函数是否存在
__declspec(dllexport) bool RC_HasFunc(
    std::string const& nameSpace,
    std::string const& funcName
) {
    return RemoteCall::hasFunc(nameSpace, funcName);
}

// 导出函数：移除函数
__declspec(dllexport) bool RC_RemoveFunc(
    std::string const& nameSpace,
    std::string const& funcName
) {
    return RemoteCall::removeFunc(nameSpace, funcName);
}

// 导出函数：移除命名空间下所有函数
__declspec(dllexport) int RC_RemoveNameSpace(
    std::string const& nameSpace
) {
    return RemoteCall::removeNameSpace(nameSpace);
}

// 导出函数：批量移除函数
__declspec(dllexport) int RC_RemoveFuncs(
    std::vector<std::pair<std::string, std::string>>& funcs
) {
    return RemoteCall::removeFuncs(funcs);
}

// 导出函数：调用错误处理
__declspec(dllexport) void RC_OnCallError(
    std::string const& msg,
    void* handle
) {
    RemoteCall::_onCallError(msg, handle);
}

} // extern "C"

} // namespace meowpapi
