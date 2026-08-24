# MeowPAPI 消费者插件集成指南

本文档描述所有使用 MeowPAPI 的消费者插件应遵循的标准化封装模式。

## 核心原则

1. **在 `load()` 阶段调用 `meowpapi::ensureLoaded()`** — 触发 MeowPAPI.dll 检测、部署和自动初始化
2. **`enable()` 阶段直接使用 API** — 无需手动调用 `initAsServer()` 等初始化函数
3. **`disable()` 阶段只清理自己的 RemoteCall 命名空间** — 不清理 MeowPAPI 全局资源
4. **仅部署者清理全局资源** — 通过 `meowpapi::isDeployer()` 判断

## 自动初始化机制

`meowpapi::ensureLoaded()` 内部执行:

1. 检测 MeowPAPI.dll 是否已由 LeviLamina 作为独立插件加载
2. 若未加载,从消费者插件嵌入的二进制数据中释放 MeowPAPI.dll 和 manifest.json 到 `plugins/MeowPAPI/`
3. `LoadLibraryW` 加载 DLL
4. `GetProcAddress` 解析所有导出函数
5. **自动初始化**(autoInit):
   - `initAsServer()` — 设置本地 PAPI 注册中心
   - `registerBuiltinPlaceholders()` — 注册内置占位符(MSPT/TPS/服务器/玩家/时间等)
   - `exportRemoteCallApi()` — 导出 MeowPAPI 命名空间的 RemoteCall API
   - `installBepApiFallback()` — 安装 BEPAPI 兼容层

多消费者插件防冲突:`std::mutex` 保证部署只执行一次。第一个加载的消费者插件部署并初始化 MeowPAPI,后续插件直接连接。

## 标准封装模式

### xmake.lua 依赖配置

```lua
-- 包含 MeowPAPI 静态库项目
includes("../MeowPAPI/xmake.lua")

target("MyPlugin")
    -- ... 其他配置 ...
    add_deps("MeowPAPI")
    add_includedirs("../MeowPAPI/include")
    add_includedirs("../MeowPAPI/src-wrapper")  -- DllLoader.h 所在目录
```

### 插件入口 (ModEntry.cpp)

```cpp
#include "meowpapi/EnsureLoaded.h"
#include "meowpapi/RemoteCallAPI.h"  // RemoteCall::exportAs / removeNameSpace

bool MyPlugin::load() {
    auto& logger = getSelf().getLogger();

    // 1. 标准化模式:在 load() 阶段触发 MeowPAPI 部署 + 自动初始化
    if (!meowpapi::ensureLoaded()) {
        logger.error("MeowPAPI 加载失败");
        return false;
    }
    return true;
}

bool MyPlugin::enable() {
    // MeowPAPI 已在 load() 阶段就绪
    // 直接注册本插件的 RemoteCall API
    RemoteCall::exportAs("MyPlugin", "myFunc", []() -> bool {
        return true;
    });
    return true;
}

bool MyPlugin::disable() {
    // 1. 清理本插件的 RemoteCall 命名空间
    RemoteCall::removeNameSpace("MyPlugin");

    // 2. 仅当本插件是 MeowPAPI 的部署者时,才清理全局资源
    if (meowpapi::isDeployer()) {
        meowpapi::removeBepApiFallback();
        meowpapi::removeRemoteCallApi();
    }
    return true;
}
```

## 客户端模式插件

如果插件需要作为 PAPI **客户端**(通过 RemoteCall 调用 MeowSidebar 而非使用本地注册中心):

```cpp
bool MyClientPlugin::load() {
    // ensureLoaded() 的 autoInit 会先设置 server 模式
    if (!meowpapi::ensureLoaded()) return false;
    return true;
}

bool MyClientPlugin::enable() {
    // 覆盖为客户端模式
    meowpapi::PlaceholderApi::getInstance().initAsClient();
    // 后续 translateString 会通过 RemoteCall 调用 MeowSidebar
    return true;
}
```

## 禁止事项

- **不要**在 `enable()` 中调用 `initAsServer()` / `registerBuiltinPlaceholders()` / `exportRemoteCallApi()` / `installBepApiFallback()` — 这些已由 `ensureLoaded()` 的 autoInit 完成
- **不要**在 `disable()` 中无条件调用 `meowpapi::removeRemoteCallApi()` — 仅部署者应清理全局资源
- **不要**直接调用 `DllLoader::getInstance().load()` — 使用公共 API `meowpapi::ensureLoaded()`

## 已遵循此模式的插件

| 插件 | 模式 | 部署者? |
|------|------|---------|
| MeowSidebar | Server | 是(通常) |
| MeowTemperature | Server | 是(若先加载) |
| MeowAchievement | Server | 是(若先加载) |

## 头文件引用

```cpp
#include "meowpapi/EnsureLoaded.h"       // ensureLoaded() / isDeployer()
#include "meowpapi/RemoteCallAPI.h"      // RemoteCall::exportAs / removeNameSpace
#include "meowpapi/PlaceholderApi.h"     // PlaceholderApi::getInstance().translateString...
```
