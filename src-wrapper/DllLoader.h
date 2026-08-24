// DllLoader.h - MeowPAPI.dll 加载器（静态库侧）
//
// 职责：
// 1. 检测 MeowPAPI.dll 是否已由 LeviLamina 作为独立插件加载（GetModuleHandleW）
// 2. 若未加载，从嵌入的数据中释放 MeowPAPI.dll 和 manifest.json 到
//    plugins/MeowPAPI/ 目录，然后 LoadLibraryW 加载
// 3. 通过 GetProcAddress 获取所有导出函数指针
// 4. 设置回调调用器，使 DLL 能回调静态库侧的 std::function
// 5. 自动初始化（initAsServer + registerBuiltinPlaceholders + exportRemoteCallApi +
//    installBepApiFallback），消费者插件无需手动调用这些
//
// 嵌入的 DLL 数据由 MeowPAPI_DLL target 的 after_build 生成（embedded_dll.h）。
// 消费者插件只需链接此静态库，MeowPAPI.dll 即被内置到消费者插件的二进制中。
//
// 多消费者插件防冲突：使用 std::call_once 保证只部署一次。第一个加载的
// 消费者插件部署 MeowPAPI.dll 并自动初始化，后续消费者插件直接连接。
#pragma once

#include "meowpapi/DllExports.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

class Player;

namespace meowpapi {

// 嵌入数据访问器（定义在 EmbeddedData.cpp 中）
const uint8_t* getEmbeddedDllData();
size_t         getEmbeddedDllSize();
const uint8_t* getEmbeddedManifestData();
size_t         getEmbeddedManifestSize();
uint32_t       getEmbeddedBuildTimestamp();

// DLL 函数指针表
struct DllFunctions {
    // 版本
    const char* (*getVersion)();
    // ABI 版本数值/功能位（旧版 DLL 无此导出时为 nullptr，运行时查询返回 0）
    uint32_t    (*getAbiVersion)();
    uint32_t    (*getAbiFeatures)();
    uint64_t    (*getBuildTimestamp)();

    // 初始化
    void (*initAsServer)();
    void (*initAsClient)();

    // 回调注册
    void (*setCallbackInvoker)(MeowPAPI_CallbackFn fn);
    // 带参数回调注册（旧版 DLL 无此导出时为 nullptr，带参功能降级不可用）
    void (*setCallbackInvokerWithParams)(MeowPAPI_CallbackWithParamsFn fn);

    // 翻译
    int (*translateString)(const char* str, char* out, int outSize);
    int (*translateStringWithPlayer)(const char* str, void* player, char* out, int outSize);
    int (*translateStringWithPlayerSkip)(const char* str, void* player, int skipRemote, char* out, int outSize);

    // 获取值
    int (*getValue)(const char* name, char* out, int outSize);
    int (*getValueWithPlayer)(const char* name, void* player, char* out, int outSize);

    // 检查
    int (*hasPlaceholder)(const char* name);

    // 注册
    int (*registerPlaceholder)(const char* pluginName, const char* name, int type, uint64_t callbackId, int updateIntervalMs);
    // 带参数注册（GMLIB PAPI 兼容；旧版 DLL 无此导出时为 nullptr）
    int (*registerPlaceholderWithParams)(const char* pluginName, const char* name, int type, uint64_t callbackId, int updateIntervalMs);
    int (*unregisterPlaceholder)(const char* name);
    void (*unregisterByPlugin)(const char* pluginName);
    void (*clear)();

    // 列表
    int (*listPlaceholders)(char* out, int outSize);
    int (*listPlaceholdersByPlugin)(const char* pluginName, char* out, int outSize);

    // 服务端
    void (*exportRemoteCallApi)();
    void (*removeRemoteCallApi)();
    void (*registerBuiltinPlaceholders)();
    int (*isBepApiAvailable)();
    void (*installBepApiFallback)();
    void (*removeBepApiFallback)();

    // 客户端
    int (*isRemoteAvailable)();
    int (*remoteTranslateString)(const char* str, char* out, int outSize);
    int (*remoteTranslateStringWithPlayer)(const char* str, const char* playerName, char* out, int outSize);
    int (*remoteGetValue)(const char* name, char* out, int outSize);
    int (*remoteGetValueWithPlayer)(const char* name, const char* playerName, char* out, int outSize);
    int (*remoteHasPlaceholder)(const char* name);
};

// DLL 加载器单例
class DllLoader {
public:
    static DllLoader& getInstance();

    // 加载 DLL（如果尚未加载）
    // 返回 true 如果成功加载或已经加载
    // 首次加载时会自动部署嵌入的 DLL 并自动初始化
    bool load(bool skipAutoInit = false);

    // 获取函数指针表
    // 如果 DLL 未加载，返回 nullptr
    DllFunctions const* functions() const;

    //===== ABI 查询（旧版 DLL 无 ABI 导出时返回 0）=====
    // 已加载 DLL 的 ABI 版本（0xMMmmpp；旧版 DLL 无此导出返回 0）
    uint32_t loadedAbiVersion() const;
    // 已加载 DLL 的功能位掩码（旧版 DLL 无此导出返回 0）
    uint32_t loadedAbiFeatures() const;
    // 便捷判断：运行时 DLL 是否支持带参占位符（GMLIB PAPI 兼容）
    bool paramPapiSupported() const;
    // 已加载 DLL 的构建时间戳（PE TimeDateStamp，Unix epoch 秒；未加载返回 0）
    // 消费者可据此与嵌入时间戳比较，诊断部署的 DLL 是否过期
    uint64_t loadedBuildTimestamp() const;

    // 是否已自动初始化（消费者插件可据此跳过重复初始化）
    bool isAutoInitialized() const { return mAutoInitialized; }

    // 是否由本实例部署了 DLL（部署者负责全局清理）
    bool isDeployed() const { return mDeployed; }

    // 注册回调并返回 callbackId
    // 静态库侧存储 std::function，DLL 通过 callbackId 回调
    uint64_t registerCallback(std::function<std::string(Player*)> cb);

    // 注册带参回调并返回 callbackId（GMLIB PAPI 兼容）
    // paramsJson 为参数键值对 JSON 对象字符串
    uint64_t registerCallbackWithParams(std::function<std::string(Player*, std::string const&)> cb);

    // 移除回调
    void unregisterCallback(uint64_t id);

private:
    DllLoader() = default;

    bool              mLoaded         = false;
    bool              mDeployed       = false; // 是否由本实例部署了 DLL
    bool              mAutoInitialized = false; // 是否已自动初始化
    DllFunctions      mFuncs{};
    void*             mModule = nullptr; // HMODULE

    // 回调存储
    std::unordered_map<uint64_t, std::function<std::string(Player*)>> mCallbacks;
    // 带参回调存储（与 mCallbacks 共用 ID 序列）
    std::unordered_map<uint64_t, std::function<std::string(Player*, std::string const&)>> mCallbacksWithParams;
    uint64_t                                                                             mNextCallbackId = 1;

    // 解析所有函数指针
    bool resolveFunctions();

    // 从嵌入数据释放 DLL 和 manifest 到 plugins/MeowPAPI/ 并加载
    // 返回 HMODULE（成功）或 nullptr（失败）
    void* deployFromEmbedded();

    // 查找 plugins/ 目录的绝对路径
    std::wstring findPluginsDir() const;

    // 自动初始化：initAsServer + registerBuiltinPlaceholders + exportRemoteCallApi + installBepApiFallback
    void autoInit();

    // 回调调用器（DLL 通过此函数回调静态库）
    static void callbackInvoker(uint64_t callbackId, void* player, char* out, int outSize);

    // 带参回调调用器（DLL 通过此函数回调静态库，携带 paramsJson）
    static void callbackInvokerWithParams(
        uint64_t callbackId, void* player, const char* paramsJson, char* out, int outSize
    );

    // ===== 版本检查 =====
    // 用嵌入数据覆盖磁盘上的 DLL 文件（不重新加载）
    // 用于版本过期时更新文件，下次重启生效
    bool updateFileFromEmbedded();

    // 版本检查：比较嵌入时间戳与磁盘/已加载版本
    // 若过期则更新文件。返回 true 表示文件被更新
    // hLoaded 为已加载模块句柄（HMODULE，以 void* 传递避免头文件依赖 Windows.h），
    // 传 nullptr 则检查磁盘文件版本
    bool checkAndUpdateVersion(void* hLoaded);
};

} // namespace meowpapi
