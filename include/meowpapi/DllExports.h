// DllExports.h - MeowPAPI.dll 导出的 C API
//
// MeowPAPI.dll 导出以下 C 接口，供静态库包装层通过 LoadLibrary + GetProcAddress 调用。
// 静态库是唯一的中间层：检测 DLL、释放嵌入的 DLL、加载、转发调用。
// 这样 MeowSidebar / MeowMenu 等消费者只需链接静态库，不需要依赖 legacyremotecall。
#pragma once

#include <cstdint>

#ifdef MEOWPAPI_DLL_EXPORTS
#define MEOWPAPI_API __declspec(dllexport)
#else
#define MEOWPAPI_API
#endif

// 回调函数类型：静态库设置此回调，DLL 在求值占位符时通过此回调调用静态库侧的 std::function
//   callbackId  - 静态库分配的回调 ID
//   player      - Player* 指针（服务器级占位符为 nullptr）
//   out         - 输出缓冲区
//   outSize     - 输出缓冲区大小（含末尾 \0）
typedef void (*MeowPAPI_CallbackFn)(uint64_t callbackId, void* player, char* out, int outSize);

// 带参数回调函数类型（GMLIB PAPI 兼容）：带参占位符求值时 DLL 通过此回调调用静态库侧的 std::function
//   callbackId  - 静态库分配的回调 ID
//   player      - Player* 指针（服务器级占位符为 nullptr）
//   paramsJson  - 参数键值对 JSON 对象字符串（如 {"score":"zxsc","number":"10"}）
//   out         - 输出缓冲区
//   outSize     - 输出缓冲区大小（含末尾 \0）
typedef void (*MeowPAPI_CallbackWithParamsFn)(uint64_t callbackId, void* player, const char* paramsJson, char* out, int outSize);

// 回调结果最大长度
#define MEOWPAPI_MAX_RESULT 8192

//===== ABI 版本（单一来源：所有版本数值/字符串/功能位均由此派生，禁止手写）=====
// 历史版本：
//   0x010000 (1.0.0) - 原始功能集：注册/翻译/静态占位符/RemoteCall 桥
//   0x010100 (1.1.0) - 带参占位符（GMLIB PAPI 兼容 ${papi:NAME,K=V}）
#define MEOWPAPI_V_MAJOR 1
#define MEOWPAPI_V_MINOR 1
#define MEOWPAPI_V_PATCH 0
#define MEOWPAPI_ABI_VERSION ((MEOWPAPI_V_MAJOR << 16) | (MEOWPAPI_V_MINOR << 8) | MEOWPAPI_V_PATCH)

// ABI 功能位掩码（GetAbiFeatures 返回值，可用 |= 组合）
// 消费者用 (features & MEOWPAPI_ABI_FEATURE_XXX) 判断运行时 DLL 是否具备某能力
#define MEOWPAPI_ABI_FEATURE_PARAMS 0x00000001 // 带参占位符（GMLIB PAPI 兼容）

// 版本字符串派生（供 GetVersion / 日志使用，勿手写）
#define MEOWPAPI_STR2(x) #x
#define MEOWPAPI_STR(x)  MEOWPAPI_STR2(x)
#define MEOWPAPI_VERSION_STRING \
    MEOWPAPI_STR(MEOWPAPI_V_MAJOR) "." MEOWPAPI_STR(MEOWPAPI_V_MINOR) "." MEOWPAPI_STR(MEOWPAPI_V_PATCH)

#ifdef __cplusplus
extern "C" {
#endif

//===== 版本 =====
MEOWPAPI_API const char* MeowPAPI_GetVersion(void);
// 返回 ABI 版本数值（0xMMmmpp，如 0x010100 = 1.1.0）
// 旧版 DLL（< 1.1.0）无此导出，GetProcAddress 返回 nullptr，视为 0
// 消费者编译期用 MEOWPAPI_ABI_VERSION 宏与运行时返回值比较，判断功能可用性
MEOWPAPI_API uint32_t MeowPAPI_GetAbiVersion(void);
// 返回 ABI 功能位掩码（MEOWPAPI_ABI_FEATURE_* 的组合）
// 消费者用位与判断运行时能力，比版本号比较更稳定（add-only 功能只增位不改值）
MEOWPAPI_API uint32_t MeowPAPI_GetAbiFeatures(void);
// 返回构建时间戳（PE TimeDateStamp，Unix epoch 秒）
// 消费者插件用此值与嵌入版本比较，检测已部署的 DLL 是否过期
MEOWPAPI_API uint64_t MeowPAPI_GetBuildTimestamp(void);

//===== 初始化 =====
MEOWPAPI_API void MeowPAPI_InitAsServer(void);
MEOWPAPI_API void MeowPAPI_InitAsClient(void);

//===== 回调注册 =====
// 静态库在 DLL 加载后立即设置回调调用器
// DLL 在求值占位符时通过此函数指针回调静态库
MEOWPAPI_API void MeowPAPI_SetCallbackInvoker(MeowPAPI_CallbackFn fn);
// 带参数回调调用器（带参占位符求值时使用，paramsJson 见上方 typedef 注释）
MEOWPAPI_API void MeowPAPI_SetCallbackInvokerWithParams(MeowPAPI_CallbackWithParamsFn fn);

//===== 翻译 =====
// 返回结果字符串长度（不含 \0）。若 out 为 NULL，返回所需长度。
// 结果写入 out 缓冲区（最多 outSize-1 字节 + \0）
MEOWPAPI_API int MeowPAPI_TranslateString(const char* str, char* out, int outSize);
MEOWPAPI_API int MeowPAPI_TranslateStringWithPlayer(const char* str, void* player, char* out, int outSize);
MEOWPAPI_API int MeowPAPI_TranslateStringWithPlayerSkip(const char* str, void* player, int skipRemote, char* out, int outSize);

//===== 获取值 =====
MEOWPAPI_API int MeowPAPI_GetValue(const char* name, char* out, int outSize);
MEOWPAPI_API int MeowPAPI_GetValueWithPlayer(const char* name, void* player, char* out, int outSize);

//===== 检查 =====
MEOWPAPI_API int MeowPAPI_HasPlaceholder(const char* name);

//===== 注册占位符 =====
// type: 0=Server, 1=Player, 2=Static
// callbackId: 静态库侧分配的回调 ID，DLL 通过回调调用器调用
// 返回 1=成功, 0=失败
MEOWPAPI_API int MeowPAPI_RegisterPlaceholder(
    const char* pluginName, const char* name, int type,
    uint64_t callbackId, int updateIntervalMs
);
// 带参数注册（GMLIB PAPI 兼容）：name 可含 <param> 插槽模板，
// 求值时 DLL 通过带参回调调用器携带 paramsJson 回调静态库
MEOWPAPI_API int MeowPAPI_RegisterPlaceholderWithParams(
    const char* pluginName, const char* name, int type,
    uint64_t callbackId, int updateIntervalMs
);
MEOWPAPI_API int MeowPAPI_UnregisterPlaceholder(const char* name);
MEOWPAPI_API void MeowPAPI_UnregisterByPlugin(const char* pluginName);
MEOWPAPI_API void MeowPAPI_Clear(void);

//===== 列表（返回 JSON 数组字符串）=====
MEOWPAPI_API int MeowPAPI_ListPlaceholders(char* out, int outSize);
MEOWPAPI_API int MeowPAPI_ListPlaceholdersByPlugin(const char* pluginName, char* out, int outSize);

//===== 服务端：RemoteCall 导出 =====
MEOWPAPI_API void MeowPAPI_ExportRemoteCallApi(void);
MEOWPAPI_API void MeowPAPI_RemoveRemoteCallApi(void);

//===== 服务端：内置占位符 =====
MEOWPAPI_API void MeowPAPI_RegisterBuiltinPlaceholders(void);

//===== 服务端：BEPAPI 兼容 =====
MEOWPAPI_API int MeowPAPI_IsBepApiAvailable(void);
MEOWPAPI_API void MeowPAPI_InstallBepApiFallback(void);
MEOWPAPI_API void MeowPAPI_RemoveBepApiFallback(void);

//===== 客户端：RemoteCall 桥接 =====
MEOWPAPI_API int MeowPAPI_IsRemoteAvailable(void);
MEOWPAPI_API int MeowPAPI_RemoteTranslateString(const char* str, char* out, int outSize);
MEOWPAPI_API int MeowPAPI_RemoteTranslateStringWithPlayer(const char* str, const char* playerName, char* out, int outSize);
MEOWPAPI_API int MeowPAPI_RemoteGetValue(const char* name, char* out, int outSize);
MEOWPAPI_API int MeowPAPI_RemoteGetValueWithPlayer(const char* name, const char* playerName, char* out, int outSize);
MEOWPAPI_API int MeowPAPI_RemoteHasPlaceholder(const char* name);

#ifdef __cplusplus
}
#endif
