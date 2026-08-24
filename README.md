# MeowPAPI

> **Minecraft Bedrock 服务器 PlaceholderAPI 中心** —— 统一的占位符注册 / 翻译 / 跨插件共享方案
> 适用于 LeviLamina (BDS) 插件生态，同时支持 C++ 原生插件与 LSE (JS) 脚本插件。

**当前版本：v1.1.0 (ABI 0x010100)**

---

## 目录

- [特性](#特性)
- [游戏内指令](#游戏内指令)
- [安装](#安装)
- [快速开始](#快速开始)
- [版本与 ABI](#版本与-abi)
- [文档](#文档)
- [已知集成插件](#已知集成插件)

---

## 特性

- **双目标架构**：`MeowPAPI.dll` 独立插件承载全部逻辑；`MeowPAPI` 静态库供 C++ 消费者嵌入集成
- **自动释放**：静态库消费者首次加载时自动把内嵌的 `MeowPAPI.dll` 释放到 `plugins/MeowPAPI/` 并加载——无需手动分发前置 DLL（`std::call_once` 防多消费者冲突）
- **三格式翻译**：`%name%`（BEPAPI 经典）、`{name}`（BetterSidebar）、`${papi:...}`（GMLIB 带参格式）同串共存
- **带参占位符**：GMLIB PAPI 兼容的 `<param>` 槽位模板注册（如 `title_score_rank_<score>_<title>_<number>`），支持显式参数与槽位参数混用、嵌套 `${...}` 递归翻译
- **LSE 运行时桥接**：`LegacyRemoteCall (lrca)` 软依赖——运行时挂载，精确符号 + PE 导出表前缀模糊匹配两级解析，**跨 lrca 编译版本二进制兼容**；lrca 缺席时自动降级（原生 C++ PAPI 不受影响）
- **ABI 版本管理**：单一来源宏体系（`MEOWPAPI_ABI_VERSION`），消费者可运行时查询 `loadedAbiVersion()` / `paramPapiSupported()` / `loadedBuildTimestamp()` 判定部署版本
- **启动自动广播**：`ServerStartedEvent` 时自动输出版本 / ABI / 构建时间 / 占位符统计 / lrca 挂载状态——远端秒判部署版本
- **BEPAPI 兼容层**：内置 BEPAPI 占位符兼容导出，旧插件无感迁移
- **线程安全**：注册表全程持锁；JS 回调统一回主线程调度

## 游戏内指令

| 指令 | 权限 | 说明 |
|------|------|------|
| `/meowpapi` | 所有玩家 | 显示版本摘要 + 帮助 |
| `/meowpapi version` | 所有玩家 | 版本 / ABI / 构建时间 / 功能位 / lrca 挂载详情 / 占位符统计 |
| `/meowpapi list [页码]` | OP（execute 内自检） | 分页列出全部已注册占位符（名称 / 类型 / 归属插件 / 带参标记） |

> 指令注册为 `CommandPermissionLevel::Any` + execute 内自检，规避 Bedrock 客户端对高权限命令的本地隐藏问题（跨服/代理场景 OP 状态同步异常时 `GameDirectors` 命令会显示"未知命令"）。
> 控制台 / 后台来源同样可执行（输出自动去色）。

## 安装

### 方式一：独立前置插件（LSE 脚本 / 不编译 C++ 的用户）

```
plugins/
└── MeowPAPI/
    ├── manifest.json
    └── MeowPAPI.dll
```

直接放置即可。lrca（LegacyRemoteCall）为**可选依赖**——存在时自动挂载 LSE 桥，缺席时仅 LSE 脚本通道不可用。

### 方式二：静态库集成（C++ 插件开发者，推荐）

在你的插件 `xmake.lua` 中：

```lua
includes("../MeowPAPI/xmake.lua")

target("MyPlugin")
    -- ... 其他配置 ...
    add_deps("MeowPAPI")                       -- 静态库（内嵌 MeowPAPI.dll）
    add_includedirs("../MeowPAPI/include")
    add_includedirs("../MeowPAPI/src-wrapper") -- wrapper 层头文件
```

```cpp
#include "meowpapi/EnsureLoaded.h"
#include "meowpapi/PlaceholderApi.h"

bool MyPlugin::load() {
    // 部署 + 加载 MeowPAPI.dll（首次自动释放到 plugins/MeowPAPI/）
    // 注意：manifest.json 中 MeowPAPI 必须声明为 optionalDependencies
    // 或不声明——硬依赖会在首次部署前拒载消费者（鸡生蛋问题）
    return meowpapi::ensureLoaded();
}
```

详见 [INTEGRATION_GUIDE.md](INTEGRATION_GUIDE.md)。

## 快速开始

### C++ 消费者

```cpp
auto& papi = meowpapi::PlaceholderApi::getInstance();

// 服务器级占位符
papi.registerServerPlaceholder("MyPlugin", "my_online", []() {
    return std::to_string(getOnlineCount());
});

// 翻译
std::string text = papi.translateStringWithPlayer(
    "§a{pl.realName} §7TPS: {server_tps}", player);
```

### LSE (JS) 插件

```js
// 1. 导出回调
ll.export((player) => "我的称号", "MyScript", "getTitle");

// 2. 注册占位符（hasExported 预检 + 重试，勿直接 ll.import）
function tryRegister() {
    if (!ll.hasExported("MeowPAPI", "registerPlayerPlaceholder")) {
        setTimeout(tryRegister, 3000);
        return;
    }
    ll.import("MeowPAPI", "registerPlayerPlaceholder")(
        "MyScript", "my_title", "MyScript", "getTitle");
}
tryRegister();
```

> **LSE 时序铁律**：MeowPAPI 的 RemoteCall 导出由后台线程在 lrca 加载后立即完成（抢在 `ServerStartedEvent` 之前），但 LSE 插件加载仍可能先于导出——**必须 `ll.hasExported` 预检 + `setTimeout` 重试**，直接 `ll.import` 未导出函数会报 "Fail to import!" 且本次启动注册永久跳过。

## 版本与 ABI

| 版本 | ABI | 功能位 | 说明 |
|------|-----|--------|------|
| 1.1.0 | 0x010100 | bit0 = 带参占位符 | 参数化 PAPI、`/meowpapi` 指令、启动版本广播、运行时 ABI 查询、LSE 桥模糊符号匹配 |
| 1.0.0 | 0x010000 | — | 初始版本：注册表 + 三格式翻译 + RemoteCall 桥 |

消费者侧运行时检查：

```cpp
if (meowpapi::loadedAbiVersion() < MEOWPAPI_ABI_VERSION) {
    logger.warn("MeowPAPI 运行时版本过低: 0x{:06x} < 编译期 0x{:06x}",
                meowpapi::loadedAbiVersion(), MEOWPAPI_ABI_VERSION);
}
```

API 遵循 **add-only 策略**：现有接口永不修改或删除，仅追加。

## 文档

| 文档 | 内容 |
|------|------|
| [API.md](API.md) | 全部调用通道参考：C++ / LSE (JS) / C ABI / RemoteCall、带参占位符、内置占位符全表、线程模型、故障排查 |
| [INTEGRATION_GUIDE.md](INTEGRATION_GUIDE.md) | 静态库集成步骤：目录结构、xmake 配置、生命周期、manifest 依赖声明规范 |

## 已知集成插件

| 插件 | 集成方式 |
|------|----------|
| MeowSidebar | 静态库（自动释放）+ 运行时 ABI 诊断 |
| MeowMenu | 静态库 |
| MeowHolographicRenderer | 静态库 |
| MeowAchievement | 静态库（自动释放） |
| MeowTombstone | 静态库（自动释放） |
| MeowItemFilter | 静态库（自动释放） |
| MeowMemeTriggers | 静态库（自动释放） |
| MeowTemperature | 静态库（自动释放） |
| FastMiner | 静态库 |
| ZXDash (ZXPanel) | 静态库 + RemoteCall 双通道 |

---

*MeowPAPI © 2026 — Built with LeviLamina*
