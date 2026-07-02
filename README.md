# MeowPAPI 静态库集成指南

> **适用于**: C++ 插件开发者
> **目标**: 在你的插件中直接链接 MeowPAPI 静态库，获得完整的 PlaceholderAPI 功能

---

## 概述

MeowPAPI 提供两种使用方式：

| 方式 | 适用场景 | 依赖 |
|------|----------|------|
| **独立前置插件** | LSE 脚本插件、不想编译 C++ 的用户 | 安装 MeowPAPI.dll |
| **静态库集成** | C++ 插件开发者，需要深度集成 PAPI | 在 xmake.lua 中 `add_deps("MeowPAPI")` |

本文档介绍**静态库集成方式**，适用于需要在 C++ 插件中直接使用 PAPI API 的开发者。

---

## 快速开始

### 1. 目录结构

确保你的插件目录与 MeowPAPI 位于同一 `plugins/` 目录下：

```
plugins/
├── MeowPAPI/           # PAPI 静态库源码
│   ├── include/
│   │   └── meowpapi/
│   │       ├── PlaceholderApi.h
│   │       ├── PlaceholderRegistry.h
│   │       ├── Builtins.h
│   │       ├── RemoteCallBridge.h
│   │       └── BepApiBridge.h
│   ├── src/
│   └── xmake.lua       # 静态库 target
│
└── YourPlugin/         # 你的插件
    ├── src/
    └── xmake.lua       # 使用 add_deps("MeowPAPI")
```

### 2. xmake.lua 配置

在你的插件 `xmake.lua` 中添加：

```lua
-- 包含 MeowPAPI 静态库项目
includes("../MeowPAPI/xmake.lua")

target("YourPlugin")
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker")

    -- ... 其他配置 ...

    -- 关键：依赖 MeowPAPI 静态库
    add_deps("MeowPAPI")
    add_includedirs("../MeowPAPI/include")

    -- 静态库会随主插件一起链接，prelink 步骤会处理符号提取
```

### 3. 头文件引用

```cpp
#include "meowpapi/PlaceholderApi.h"
#include "meowpapi/PlaceholderRegistry.h"
#include "meowpapi/Builtins.h"       // 可选：内置占位符
#include "meowpapi/RemoteCallBridge.h"  // 可选：导出 RemoteCall API
#include "meowpapi/BepApiBridge.h"   // 可选：BEPAPI 兼容层
```

---

## 核心 API

### PlaceholderApi — 主入口

```cpp
namespace meowpapi {

class PlaceholderApi {
public:
    static PlaceholderApi& getInstance();

    // 初始化为服务端模式（直接使用本地 Registry）
    void initAsServer();

    // 初始化为客户端模式（通过 RemoteCall 连接到 MeowSidebar 等）
    void initAsClient();

    // 翻译字符串（无玩家）
    std::string translateString(std::string const& str);

    // 翻译字符串（带玩家）
    std::string translateStringWithPlayer(std::string const& str, Player* player);

    // 获取占位符值（无玩家）
    std::string getValue(std::string const& name);

    // 获取占位符值（带玩家）
    std::string getValueWithPlayer(std::string const& name, Player* player);
};

}
```

### PlaceholderRegistry — 注册占位符

```cpp
namespace meowpapi {

class PlaceholderRegistry {
public:
    static PlaceholderRegistry& getInstance();

    // 注册占位符

    // 服务器级（无玩家参数，后台线程可调用）
    bool registerServerPlaceholder(
        std::string const& pluginName,
        std::string const& papiName,
        std::function<std::string()> callback
    );

    // 玩家级（需要 Player* 参数，仅主线程调用）
    bool registerPlayerPlaceholder(
        std::string const& pluginName,
        std::string const& papiName,
        std::function<std::string(Player*)> callback
    );

    // 静态级（定时缓存，后台线程可调用）
    bool registerStaticPlaceholder(
        std::string const& pluginName,
        std::string const& papiName,
        std::function<std::string()> callback,
        int updateIntervalMs = 1000
    );

    // Remote 版本（标记为主线程专用）

    // 服务器级 Remote（主线程专用，后台线程翻译时跳过）
    bool registerServerPlaceholderRemote(
        std::string const& pluginName,
        std::string const& papiName,
        std::function<std::string()> callback
    );

    // 玩家级 Remote（主线程专用）
    bool registerPlayerPlaceholderRemote(
        std::string const& pluginName,
        std::string const& papiName,
        std::function<std::string(Player*)> callback
    );

    // 静态级 Remote（主线程专用）
    bool registerStaticPlaceholderRemote(
        std::string const& pluginName,
        std::string const& papiName,
        std::function<std::string()> callback,
        int updateIntervalMs = 1000
    );

    // 管理

    bool unregisterPlaceholder(std::string const& papiName);
    bool hasPlaceholder(std::string const& papiName);
    std::vector<std::string> listPlaceholders();

    // 查询

    std::string getValue(std::string const& name);
    std::string getValueWithPlayer(std::string const& name, Player* player);
    std::string translateString(std::string const& str);
    std::string translateStringWithPlayer(std::string const& str, Player* player);
};

}
```

---

## 使用示例

### 示例 1：初始化 PAPI 服务端

```cpp
#include "meowpapi/PlaceholderApi.h"
#include "meowpapi/Builtins.h"
#include "meowpapi/RemoteCallBridge.h"

bool MyPlugin::enable() {
    auto& logger = getSelf().getLogger();

    // 1. 初始化为服务端模式
    meowpapi::PlaceholderApi::getInstance().initAsServer();

    // 2. 注册内置占位符（可选，提供 42 个常用占位符）
    meowpapi::registerBuiltinPlaceholders();

    // 3. 导出 RemoteCall API（供 LSE 插件调用）
    meowpapi::exportRemoteCallApi();

    // 4. 安装 BEPAPI 兼容层（可选）
    meowpapi::installBepApiFallback();

    logger.info("PAPI 服务端已启动");
    return true;
}

bool MyPlugin::disable() {
    meowpapi::removeBepApiFallback();
    meowpapi::removeRemoteCallApi();
    return true;
}
```

### 示例 2：注册自定义占位符

```cpp
#include "meowpapi/PlaceholderRegistry.h"

void registerMyPlaceholders() {
    auto& registry = meowpapi::PlaceholderRegistry::getInstance();

    // 服务器级占位符：返回服务器自定义统计
    registry.registerServerPlaceholder(
        "MyPlugin",
        "my_stat",
        []() -> std::string {
            return std::to_string(getCustomStat());
        }
    );

    // 玩家级占位符：返回玩家自定义数据
    registry.registerPlayerPlaceholder(
        "MyPlugin",
        "my_score",
        [](Player* player) -> std::string {
            auto data = getPlayerData(player->getRealName());
            return std::to_string(data.score);
        }
    );

    // 静态占位符：定时缓存，减少计算开销
    registry.registerStaticPlaceholder(
        "MyPlugin",
        "my_cached_value",
        []() -> std::string {
            return computeExpensiveValue();
        },
        5000  // 5秒刷新一次
    );
}
```

### 示例 3：翻译字符串

```cpp
#include "meowpapi/PlaceholderApi.h"

void showWelcomeMessage(Player* player) {
    auto& papi = meowpapi::PlaceholderApi::getInstance();

    // 翻译带玩家占位符的字符串
    std::string msg = papi.translateStringWithPlayer(
        "欢迎 %pl.realName%! 你的血量是 %pl.health%",
        player
    );

    player->sendMessage(msg);
}

void broadcastServerInfo() {
    auto& papi = meowpapi::PlaceholderApi::getInstance();

    // 翻译服务器级字符串
    std::string info = papi.translateString(
        "服务器 TPS: %server_tps%, 在线: %server_online%"
    );

    // 广播给所有玩家
    broadcastToAll(info);
}
```

### 示例 4：直接获取占位符值

```cpp
#include "meowpapi/PlaceholderApi.h"

void logPlayerInfo(Player* player) {
    auto& papi = meowpapi::PlaceholderApi::getInstance();

    // 直接获取玩家占位符值
    std::string health = papi.getValueWithPlayer("pl.health", player);
    std::string pos = papi.getValueWithPlayer("pl.pos", player);

    // 直接获取服务器占位符值
    std::string tps = papi.getValue("server_tps");

    logger.info("玩家 {} 血量 {} 坐标 {} TPS {}",
        player->getRealName(), health, pos, tps);
}
```

---

## 线程模型详解

### 后台线程 vs 主线程

MeowSidebar 使用双线程模型刷新侧边栏：

1. **后台线程**（`refreshAsync`）：定时调用线程安全的占位符
2. **主线程**（`flushPending`）：调用需要访问 Player/Level 等非线程安全 API 的占位符

### 注册占位符时的选择

| 注册方法 | 调用线程 | 适用场景 |
|----------|----------|----------|
| `registerServerPlaceholder` | 后台线程 | 只访问线程安全 API（chrono、全局变量等） |
| `registerPlayerPlaceholder` | 主线程 | 需要访问 Player 对象 |
| `registerStaticPlaceholder` | 后台线程 | 定时缓存，减少计算开销 |
| `registerServerPlaceholderRemote` | 主线程 | 需要 `mainThreadOnly=true` 标记 |
| `registerPlayerPlaceholderRemote` | 主线程 | 需要 `mainThreadOnly=true` 标记 |
| `registerStaticPlaceholderRemote` | 主线程 | 需要 `mainThreadOnly=true` 标记 |

### 线程安全规则

**后台线程可访问的 API**：
- `std::chrono` 时间函数
- Windows API（如 `GetProcessMemoryInfo`）
- `ll::getGameVersion()` 等全局查询
- 你自己的线程安全数据结构（需加锁）

**仅主线程可访问的 API**：
- `Player*` 对象的所有方法
- `Level*` 对象的所有方法
- `NetworkHandler` 等游戏对象
- LSE RemoteCall 回调（LSE 单线程）

---

## 与 RemoteCall 的关系

### 导出 RemoteCall API

如果你的插件是 **PAPI 服务端**（即第一个加载的 PAPI 提供者），应该导出 RemoteCall API：

```cpp
// 导出完整的 PAPI 接口到 "MeowPAPI" 命名空间
meowpapi::exportRemoteCallApi();

// LSE 插件可通过 ll.import("MeowPAPI", "translateString") 等调用
```

导出的接口包括：

| 接口名 | 功能 |
|--------|------|
| `registerServerPlaceholder` | 注册服务器占位符 |
| `registerPlayerPlaceholder` | 注册玩家占位符 |
| `registerStaticPlaceholder` | 注册静态占位符 |
| `unRegisterPlaceholder` | 注销占位符 |
| `GetValue` | 获取值（无玩家） |
| `GetValueWithPlayer` | 获取值（带玩家） |
| `translateString` | 翻译字符串 |
| `translateStringWithPlayer` | 翻译字符串（带玩家） |
| `hasPlaceholder` | 检查是否存在 |
| `listPlaceholders` | 列出所有占位符 |

### 作为 PAPI 客户端

如果你的插件**不是**第一个加载的 PAPI 提供者（如 MeowSidebar 已加载），可以初始化为客户端模式：

```cpp
// 通过 RemoteCall 连接到已存在的 PAPI 服务端
meowpapi::PlaceholderApi::getInstance().initAsClient();

// 翻译等操作会自动路由到服务端
std::string result = meowpapi::PlaceholderApi::getInstance().translateString("...");
```

或者直接使用 RemoteCall 桥接函数：

```cpp
#include "meowpapi/RemoteCallBridge.h"

// 检查是否有 PAPI 服务端
if (meowpapi::isRemoteAvailable()) {
    // 通过 RemoteCall 翻译
    std::string result = meowpapi::remoteTranslateString("TPS: %server_tps%");
}
```

---

## BEPAPI 兼容层

如果你的服务器还安装了 BEPAPI（另一个 PlaceholderAPI 实现），可以启用兼容回退：

```cpp
#include "meowpapi/BepApiBridge.h"

// 安装回退层：当 MeowPAPI 找不到占位符时，尝试查询 BEPAPI
meowpapi::installBepApiFallback();

// 移除回退层
meowpapi::removeBepApiFallback();
```

---

## 冲突检测

如果多个插件都尝试初始化 PAPI 服务端，会产生 RemoteCall 命名空间冲突。推荐做法：

```cpp
#include "meowpapi/RemoteCallBridge.h"

bool MyPlugin::enable() {
    // 检测是否已有 PAPI 服务端
    if (meowpapi::isRemoteAvailable()) {
        logger.warn("已存在 PAPI 服务端，跳过初始化");
        return true;
    }

    // 初始化为服务端
    meowpapi::PlaceholderApi::getInstance().initAsServer();
    meowpapi::registerBuiltinPlaceholders();
    meowpapi::exportRemoteCallApi();

    return true;
}
```

---

## 完整插件示例

```cpp
// MyPlugin.cpp
#include "MyPlugin.h"

#include "meowpapi/PlaceholderApi.h"
#include "meowpapi/PlaceholderRegistry.h"
#include "meowpapi/Builtins.h"
#include "meowpapi/RemoteCallBridge.h"

namespace myplugin {

MyPlugin& MyPlugin::getInstance() {
    static MyPlugin instance;
    return instance;
}

bool MyPlugin::load() {
    return true;
}

bool MyPlugin::enable() {
    auto& logger = getSelf().getLogger();

    // 检测冲突
    if (meowpapi::isRemoteAvailable()) {
        logger.warn("已存在 PAPI 服务端（可能 MeowSidebar 已加载），跳过初始化");
        return true;
    }

    // 初始化 PAPI 服务端
    meowpapi::PlaceholderApi::getInstance().initAsServer();
    meowpapi::registerBuiltinPlaceholders();

    // 注册自定义占位符
    auto& registry = meowpapi::PlaceholderRegistry::getInstance();

    registry.registerPlayerPlaceholder(
        "MyPlugin",
        "my_rank",
        [](Player* player) -> std::string {
            return getPlayerRank(player->getRealName());
        }
    );

    registry.registerStaticPlaceholder(
        "MyPlugin",
        "my_server_stat",
        []() -> std::string {
            return std::to_string(getServerStat());
        },
        2000
    );

    // 导出 RemoteCall API
    meowpapi::exportRemoteCallApi();

    logger.info("MyPlugin PAPI 服务端已启动");
    return true;
}

bool MyPlugin::disable() {
    meowpapi::removeRemoteCallApi();
    return true;
}

} // namespace myplugin

LL_REGISTER_MOD(myplugin::MyPlugin, myplugin::MyPlugin::getInstance);
```

---

## xmake.lua 完整示例

```lua
add_rules("mode.debug", "mode.release")

local local_repo = path.join(os.projectdir(), ".xmake", os.host(), os.arch(), "repositories", "liteldev-repo")
if os.exists(local_repo) then
    add_repositories("liteldev-repo " .. local_repo)
else
    add_repositories("liteldev-repo https://github.com/LiteLDev/xmake-repo.git")
end

add_requires("levilamina 26.10.3", {configs = {target_type = "server"}})
add_requires("levibuildscript")
add_requires("legacyremotecall main", {configs = {target_type = "server"}})
add_requires("magic_enum v0.9.7")

-- 包含 MeowPAPI 静态库
includes("../MeowPAPI/xmake.lua")

target("MyPlugin")
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker")

    add_cxflags("/EHa", "/utf-8", "/W4", "/Zm2000", "/wd4100", {force = true})
    add_defines("NOMINMAX", "UNICODE", "_AMD64_", "LL_MEMORY_OPERATORS")

    add_packages("levilamina")
    add_packages("legacyremotecall")
    add_packages("magic_enum")

    set_exceptions("on")
    set_kind("shared")
    set_languages("c++23")

    -- 依赖 MeowPAPI 静态库
    add_deps("MeowPAPI")
    add_includedirs("../MeowPAPI/include")

    add_files("src/MyPlugin.cpp")
    add_files("src/MemoryOperators.cpp")

    add_includedirs("src")
    set_symbols("hidden")
```

---

## 总结

| 步骤 | 操作 |
|------|------|
| 1 | `includes("../MeowPAPI/xmake.lua")` |
| 2 | `add_deps("MeowPAPI")` |
| 3 | `add_includedirs("../MeowPAPI/include")` |
| 4 | `#include "meowpapi/PlaceholderApi.h"` |
| 5 | `PlaceholderApi::getInstance().initAsServer()` |
| 6 | 使用 `PlaceholderRegistry` 注册占位符 |
| 7 | 使用 `PlaceholderApi` 翻译字符串 |

---

## 源码位置

```
plugins/MeowPAPI/
├── include/meowpapi/
│   ├── PlaceholderApi.h      # 主入口
│   ├── PlaceholderRegistry.h # 注册 API
│   ├── Builtins.h            # 内置占位符
│   ├── RemoteCallBridge.h    # RemoteCall 桥接
│   └── BepApiBridge.h        # BEPAPI 兼容
└── src/
    ├── PlaceholderApi.cpp
    ├── PlaceholderRegistry.cpp
    ├── Builtins.cpp
    ├── RemoteCallBridge.cpp
    └── BepApiBridge.cpp
```
