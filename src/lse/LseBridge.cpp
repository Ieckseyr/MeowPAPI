// LseBridge.cpp - lrca 运行时挂载实现（精确 + 导出表模糊两级符号解析）
//
// 背景（2026-08-24 云测试服实证）：云测试服 LegacyRemoteCall.dll 与本地
// 符号快照不匹配（不同编译版本的 MSVC STL mangling 差异），GetProcAddress
// 精确匹配全失败 → 旧实现 gAttachFailed 置位且首次 attach 无 mod 上下文
// 时诊断日志被静默跳过 → LSE 桥永久禁用，所有 ll.import("MeowPAPI",...)
// 报 "has not been exported"。
//
// 修复：精确失败后解析 PE 导出表按前缀模糊匹配（"?exportFunc@RemoteCall@@YA"
// 等 7 个前缀）。MSVC ABI 自 2015 起 std::function/std::string 布局稳定，
// 前缀命中的函数二进制兼容。诊断快照经 status()/statusText() 暴露，
// /meowpapi version 可远端秒判挂载细节。
#include "lse/LseBridge.h"
#include "meowpapi/RemoteCallAPI.h"

#include "ll/api/mod/NativeMod.h"

#include <Windows.h>

#include <cstring>
#include <mutex>

namespace meowpapi::lse {

// lrca 导出函数的运行时解析指针（attach() 成功后非空）。
// 放在 detail 命名空间（非匿名）：文件末尾的 RemoteCall:: 转发函数
// 需要跨命名空间访问
namespace detail {

using ExportFuncFn      = bool (*)(std::string const&, std::string const&, RemoteCall::CallbackFn&&, void*);
using ImportFuncFn      = RemoteCall::CallbackFn const& (*)(std::string const&, std::string const&);
using HasFuncFn         = bool (*)(std::string const&, std::string const&);
using RemoveFuncFn      = bool (*)(std::string const&, std::string const&);
using RemoveFuncsFn     = int (*)(std::vector<std::pair<std::string, std::string>>&);
using RemoveNameSpaceFn = int (*)(std::string const&);
using OnCallErrorFn     = void (*)(std::string const&, void*);

ExportFuncFn      p_exportFunc      = nullptr;
ImportFuncFn      p_importFunc      = nullptr;
HasFuncFn         p_hasFunc         = nullptr;
RemoveFuncFn      p_removeFunc      = nullptr;
RemoveFuncsFn     p_removeFuncs     = nullptr;
RemoveNameSpaceFn p_removeNameSpace = nullptr;
OnCallErrorFn     p_onCallError     = nullptr;

} // namespace detail

namespace {

// LegacyRemoteCall.dll 0.18.0 导出的 C++ 函数完整 mangled 符号
// （dumpbin /exports 校验, 2026-08 快照）——精确匹配用
constexpr char const* kSymbolExportFunc =
    "?exportFunc@RemoteCall@@YA_NAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0$$QEAV?$function@"
    "$$A6A?AUValueType@RemoteCall@@V?$vector@UValueType@RemoteCall@@V?$allocator@UValueType@RemoteCall@@@std@@@std@@@"
    "Z@std@@@Z@3@PEAX@Z";
constexpr char const* kSymbolImportFunc =
    "?importFunc@RemoteCall@@YAAEBV?$function@$$A6A?AUValueType@RemoteCall@@V?$vector@UValueType@RemoteCall@@V?$??"
    "allocator@UValueType@RemoteCall@@@std@@@std@@@Z@std@@AEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@"
    "D@2@@std@@3@0@Z";
constexpr char const* kSymbolHasFunc =
    "?hasFunc@RemoteCall@@YA_NAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z";
constexpr char const* kSymbolRemoveFunc =
    "?removeFunc@RemoteCall@@YA_NAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z";
constexpr char const* kSymbolRemoveFuncs =
    "?removeFuncs@RemoteCall@@YAHAEAV?$vector@U?$pair@V?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@"
    "V12@@std@@V?$allocator@U?$pair@V?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@V12@@std@@@2@@std@@"
    "@Z";
constexpr char const* kSymbolRemoveNameSpace =
    "?removeNameSpace@RemoteCall@@YAHAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z";
constexpr char const* kSymbolOnCallError =
    "?_onCallError@RemoteCall@@YAXAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PEAX@Z";

// 模糊匹配前缀（导出表扫描用）：截断到函数名+调用约定为止，
// 忽略后续参数类型的 STL mangling 差异。
// 注意 "?removeFunc@" 不会误中 "?removeFuncs@"（第 12 字符 '@' vs 's'）
constexpr char const* kPrefixExportFunc      = "?exportFunc@RemoteCall@@YA";
constexpr char const* kPrefixImportFunc      = "?importFunc@RemoteCall@@YA";
constexpr char const* kPrefixHasFunc         = "?hasFunc@RemoteCall@@YA";
constexpr char const* kPrefixRemoveFunc      = "?removeFunc@RemoteCall@@YA";
constexpr char const* kPrefixRemoveFuncs     = "?removeFuncs@RemoteCall@@YA";
constexpr char const* kPrefixRemoveNameSpace = "?removeNameSpace@RemoteCall@@YA";
constexpr char const* kPrefixOnCallError     = "?_onCallError@RemoteCall@@YA";

std::mutex gAttachMutex;
bool       gAttached     = false;
bool       gAttachFailed = false;
Status     gStatus; // 诊断快照（gAttachMutex 保护）

template <typename Fn>
Fn resolve(HMODULE module, char const* symbol) {
    if (!module) return nullptr;
    return reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(module, symbol)));
}

// PE 导出表前缀模糊解析：返回首个以 prefix 开头的导出函数地址。
// 手动解析 IMAGE_EXPORT_DIRECTORY（模块已加载，RVA + 基址即可），
// 不依赖 dbghelp/psapi。
void* fuzzyResolve(HMODULE module, char const* prefix) {
    if (!module) return nullptr;
    auto base = reinterpret_cast<uint8_t*>(module);
    auto dos  = reinterpret_cast<IMAGE_DOS_HEADER const*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS const*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    auto const& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (dir.VirtualAddress == 0 || dir.Size == 0) return nullptr;
    auto exp   = reinterpret_cast<IMAGE_EXPORT_DIRECTORY const*>(base + dir.VirtualAddress);
    auto names = reinterpret_cast<uint32_t const*>(base + exp->AddressOfNames);
    auto funcs = reinterpret_cast<uint32_t const*>(base + exp->AddressOfFunctions);
    auto ords  = reinterpret_cast<uint16_t const*>(base + exp->AddressOfNameOrdinals);
    size_t prefixLen = std::strlen(prefix);
    for (DWORD i = 0; i < exp->NumberOfNames; i++) {
        char const* name = reinterpret_cast<char const*>(base + names[i]);
        if (std::strncmp(name, prefix, prefixLen) == 0) {
            return base + funcs[ords[i]];
        }
    }
    return nullptr;
}

template <typename Fn>
Fn fuzzyResolveFn(HMODULE module, char const* prefix) {
    return reinterpret_cast<Fn>(fuzzyResolve(module, prefix));
}

// 候选模块列表：独立 lrca 优先，LSE 引擎 DLL 兜底（若引擎内置 RemoteCall）
struct CandidateModule {
    wchar_t const* dllName;
    char const*    friendlyName;
};

CandidateModule const kCandidates[] = {
    {L"LegacyRemoteCall.dll",              "LegacyRemoteCall"},
    {L"legacy-script-engine-quickjs.dll",  "LSE-quickjs"},
    {L"legacy-script-engine-nodejs.dll",   "LSE-nodejs"},
};

// 对单个模块做两级符号解析；全部 7 个符号解析成功返回 true（outFuzzy 标记级别）
bool tryResolveAll(HMODULE module, bool& outFuzzy) {
    // 第一级：精确 mangled 符号
    detail::p_exportFunc      = resolve<detail::ExportFuncFn>(module, kSymbolExportFunc);
    detail::p_importFunc      = resolve<detail::ImportFuncFn>(module, kSymbolImportFunc);
    detail::p_hasFunc         = resolve<detail::HasFuncFn>(module, kSymbolHasFunc);
    detail::p_removeFunc      = resolve<detail::RemoveFuncFn>(module, kSymbolRemoveFunc);
    detail::p_removeFuncs     = resolve<detail::RemoveFuncsFn>(module, kSymbolRemoveFuncs);
    detail::p_removeNameSpace = resolve<detail::RemoveNameSpaceFn>(module, kSymbolRemoveNameSpace);
    detail::p_onCallError     = resolve<detail::OnCallErrorFn>(module, kSymbolOnCallError);

    bool allExact = detail::p_exportFunc && detail::p_importFunc && detail::p_hasFunc
                 && detail::p_removeFunc && detail::p_removeFuncs && detail::p_removeNameSpace
                 && detail::p_onCallError;
    if (allExact) {
        outFuzzy = false;
        return true;
    }

    // 第二级：导出表前缀模糊匹配（跨 lrca 编译版本的 STL mangling 差异）
    detail::p_exportFunc      = fuzzyResolveFn<detail::ExportFuncFn>(module, kPrefixExportFunc);
    detail::p_importFunc      = fuzzyResolveFn<detail::ImportFuncFn>(module, kPrefixImportFunc);
    detail::p_hasFunc         = fuzzyResolveFn<detail::HasFuncFn>(module, kPrefixHasFunc);
    detail::p_removeFunc      = fuzzyResolveFn<detail::RemoveFuncFn>(module, kPrefixRemoveFunc);
    detail::p_removeFuncs     = fuzzyResolveFn<detail::RemoveFuncsFn>(module, kPrefixRemoveFuncs);
    detail::p_removeNameSpace = fuzzyResolveFn<detail::RemoveNameSpaceFn>(module, kPrefixRemoveNameSpace);
    detail::p_onCallError     = fuzzyResolveFn<detail::OnCallErrorFn>(module, kPrefixOnCallError);

    bool allFuzzy = detail::p_exportFunc && detail::p_importFunc && detail::p_hasFunc
                 && detail::p_removeFunc && detail::p_removeFuncs && detail::p_removeNameSpace
                 && detail::p_onCallError;
    if (allFuzzy) {
        outFuzzy = true;
        return true;
    }
    return false;
}

void clearResolved() {
    detail::p_exportFunc      = nullptr;
    detail::p_importFunc      = nullptr;
    detail::p_hasFunc         = nullptr;
    detail::p_removeFunc      = nullptr;
    detail::p_removeFuncs     = nullptr;
    detail::p_removeNameSpace = nullptr;
    detail::p_onCallError     = nullptr;
}

} // namespace

bool attach() {
    std::lock_guard<std::mutex> lock(gAttachMutex);
    if (gAttached) return true;
    if (gAttachFailed) return false; // 每进程只尝试一轮, 避免反复扫导出表

    gStatus.tried = true;

    bool foundModule = false;
    for (auto const& cand : kCandidates) {
        HMODULE mod = ::GetModuleHandleW(cand.dllName);
        if (!mod) continue;
        foundModule = true;
        gStatus.moduleName = cand.friendlyName;

        bool fuzzy = false;
        if (tryResolveAll(mod, fuzzy)) {
            gAttached        = true;
            gStatus.attached = true;
            gStatus.fuzzy    = fuzzy;
            return true;
        }
        // 该模块在但符号不全：清空后继续试下一候选（引擎 DLL 可能不含
        // 完整 RemoteCall 导出，不应阻塞后续候选）
        clearResolved();
    }

    // 模块在但所有候选的符号两级解析均失败 → 版本彻底不兼容，置失败标志。
    // 模块不在 → 不置失败标志（模块稍后加载时下次 attach 可重试）
    if (foundModule) {
        gAttachFailed  = true;
        gStatus.failed = true;
        // 诊断日志：NativeMod::current() 无 mod 上下文时为空
        // （DllLoader 早期调用），判空跳过——细节经 statusText()
        // 由 /meowpapi version 与启动广播展示
        if (auto mod2 = ll::mod::NativeMod::current()) {
            mod2->getLogger().warn(
                "[LseBridge] {} 已加载但符号两级解析(精确+模糊)均失败 —— "
                "lrca 版本与本插件不兼容, LSE 桥接不可用 (原生 C++ PAPI 不受影响)",
                gStatus.moduleName
            );
        }
    }
    return false;
}

bool isAttached() { return gAttached; }

Status status() {
    std::lock_guard<std::mutex> lock(gAttachMutex);
    return gStatus;
}

std::string statusText() {
    std::lock_guard<std::mutex> lock(gAttachMutex);
    if (gAttached) {
        std::string s = "已挂载(" + gStatus.moduleName;
        if (gStatus.fuzzy) s += ", 模糊匹配";
        s += ", LSE可用)";
        return s;
    }
    if (gStatus.failed) {
        return "模块已加载但符号不匹配(" + gStatus.moduleName + ")";
    }
    if (gStatus.tried) {
        return "未找到 lrca 模块(LSE 不可用)";
    }
    return "未初始化";
}

} // namespace meowpapi::lse

//===== RemoteCall 命名空间函数定义（DLL 模式，软依赖）=====
// 替代原 __declspec(dllimport) 直接导入：未挂载时安全降级，
// 挂载后转发到 lrca 导出的原始函数。
// 注意：默认参数在 RemoteCallAPI.h 的声明处，此处定义不重复。

namespace RemoteCall {

// 本地空回调（替代原版从 lrca 导入的 EMPTY_FUNC，语义一致：
// 空 std::function，调用侧 !fn 判定为 true）
CallbackFn const EMPTY_FUNC{};

bool exportFunc(std::string const& nameSpace, std::string const& funcName, CallbackFn&& callback, void* handle) {
    if (!meowpapi::lse::isAttached()) return false;
    return meowpapi::lse::detail::p_exportFunc(nameSpace, funcName, std::move(callback), handle);
}

CallbackFn const& importFunc(std::string const& nameSpace, std::string const& funcName) {
    if (!meowpapi::lse::isAttached()) return EMPTY_FUNC;
    return meowpapi::lse::detail::p_importFunc(nameSpace, funcName);
}

bool hasFunc(std::string const& nameSpace, std::string const& funcName) {
    if (!meowpapi::lse::isAttached()) return false;
    return meowpapi::lse::detail::p_hasFunc(nameSpace, funcName);
}

bool removeFunc(std::string const& nameSpace, std::string const& funcName) {
    if (!meowpapi::lse::isAttached()) return false;
    return meowpapi::lse::detail::p_removeFunc(nameSpace, funcName);
}

int removeFuncs(std::vector<std::pair<std::string, std::string>>& funcs) {
    if (!meowpapi::lse::isAttached()) return 0;
    return meowpapi::lse::detail::p_removeFuncs(funcs);
}

int removeNameSpace(std::string const& nameSpace) {
    if (!meowpapi::lse::isAttached()) return 0;
    return meowpapi::lse::detail::p_removeNameSpace(nameSpace);
}

void _onCallError(std::string const& msg, void* handle) {
    if (!meowpapi::lse::isAttached()) return;
    meowpapi::lse::detail::p_onCallError(msg, handle);
}

} // namespace RemoteCall
