# MeowPAPI API 调用文档

> 适用版本：MeowPAPI 1.1.0（ABI 0x010100）
> 本文档覆盖全部调用通道：C++ 静态库、LSE (JS)、C ABI、RemoteCall 代理。

---

## 目录

1. [架构概述](#1-架构概述)
2. [快速开始](#2-快速开始)
3. [翻译格式](#3-翻译格式)
4. [C++ API（PlaceholderApi）](#4-c-apiplaceholderapi)
5. [C++ API（PlaceholderRegistry 直用）](#5-c-apiplaceholderregistry-直用)
6. [LSE (JS) API](#6-lse-js-api)
7. [带参占位符（GMLIB PAPI 兼容）](#7-带参占位符gmlib-papi-兼容)
8. [ABI 版本与功能位](#8-abi-版本与功能位)
9. [内置占位符全表](#9-内置占位符全表)
10. [线程模型](#10-线程模型)
11. [C ABI 参考（DllExports）](#11-c-abi-参考dllexports)
12. [RemoteCall 代理（RemoteCallAPI.h）](#12-remotecall-代理remotecallapih)
13. [错误处理与常见问题](#13-错误处理与常见问题)
14. [游戏内指令（/meowpapi）](#14-游戏内指令meowpapi)

---

## 1. 架构概述

MeowPAPI 采用 **双目标架构**：

| 目标 | 类型 | 职责 |
|------|------|------|
| `MeowPAPI_DLL` | LeviLamina 插件 DLL | 全部实际逻辑：注册表、内置占位符、RemoteCall 桥、BEPAPI 兼容 |
| `MeowPAPI` | 静态库（wrapper） | 嵌入 DLL 二进制 → 检测/部署/加载 → 转发调用 |

消费者插件只需链接静态库。首次加载时，静态库把 `MeowPAPI.dll` 释放到
`plugins/MeowPAPI/` 并 `LoadLibraryW` 加载，随后自动完成初始化
（`initAsServer` + 注册内置占位符 + 导出 RemoteCall API + 安装 BEPAPI 兼容层）。
多个消费者插件防冲突：`std::call_once` 保证只部署一次，第一个加载的是"部署者"。

```
┌─────────────┐  静态库(wrapper)  ┌──────────────────┐
│ MeowSidebar │ ─┐                │                  │
│ MeowMenu    │ ─┼──add_deps────> │ MeowPAPI.dll     │ <── LeviLamina 独立插件
│ ZXPanel ... │ ─┘                │ (注册表+内置PAPI) │
└─────────────┘                  └────────┬─────────┘
                                          │ RemoteCall (MeowPAPI 命名空间)
                                   ┌──────┴──────┐
                                   │ LSE/JS 插件  │
                                   └─────────────┘
```

集成方式（xmake 依赖、load/enable/disable 生命周期）见
[INTEGRATION_GUIDE.md](INTEGRATION_GUIDE.md)。核心三步：

```lua
-- xmake.lua
includes("../MeowPAPI/xmake.lua")
target("MyPlugin")
    add_deps("MeowPAPI")
    add_includedirs("../MeowPAPI/include")
    add_includedirs("../MeowPAPI/src-wrapper")
```

```cpp
// load() 阶段：触发部署 + 自动初始化（勿在 enable() 重复 init）
if (!meowpapi::ensureLoaded()) return false;
```

---

## 2. 快速开始

### C++ 消费者

```cpp
#include "meowpapi/EnsureLoaded.h"
#include "meowpapi/PlaceholderApi.h"

bool MyPlugin::load() {
    return meowpapi::ensureLoaded();   // 部署 + 自动初始化
}

bool MyPlugin::enable() {
    auto& papi = meowpapi::PlaceholderApi::getInstance();

    // 注册服务器级占位符：{my_online} → "12"
    papi.registerServerPlaceholder("MyPlugin", "my_online", [](Player* /*player*/) {
        return std::to_string(/* ... */ 12);
    });

    // 翻译
    std::string text = papi.translateStringWithPlayer(
        "§a{pl.realName} §7TPS: {server_tps}", player);
    return true;
}
```

### LSE (JS) 插件

```js
// 1. 导出回调
ll.export((player) => {
    return "我的称号";
}, "MyScript", "getTitle");

// 2. 注册玩家级占位符 {my_title}
ll.import("MeowPAPI", "registerPlayerPlaceholder")(
    "MyScript",      // pluginName      —— 注册归属（卸载时按此清理）
    "my_title",      // papiName        —— 占位符名
    "MyScript",      // callbackPlugin  —— 回调所属 RemoteCall 命名空间
    "getTitle"       // callbackFunc    —— 回调函数名
);

// 3. 翻译（也可交给 MeowSidebar 等消费者在显示时翻译）
let text = ll.import("MeowPAPI", "translateStringWithPlayer")(
    "§d{my_title} §7{pl.realName}", "Steve"
);
```

---

## 3. 翻译格式

`translateString*` 系列同时支持三种格式：

| 格式 | 示例 | 说明 |
|------|------|------|
| `%name%` | `%server_tps%` | BEPAPI 经典格式 |
| `{name}` | `{pl.realName}` | BetterSidebar 格式 |
| `${papi:...}` | `${papi:rank_<n>,<n>=10}` | GMLIB PAPI 带参格式，见 §7 |

规则：

- **`{}` 格式含冒号时跳过不处理**（如 `{js:expr}`、`{E:name}`），保留原文。
- **未命中**：占位符保留为 `{name}` 原样返回（便于第二遍翻译再次匹配），
  `%name%` 格式未命中同样回退为 `{name}` 形式。
- **回调内部错误 / 回调缺失**：输出 `{ERR}`。
- `${papi:...}` 嵌套感知配对大括号，值内可嵌套任意 `${...}` 表达式。

---

## 4. C++ API（PlaceholderApi）

头文件：`meowpapi/PlaceholderApi.h`。单例自动路由——服务端模式走本地注册表，
客户端模式走 RemoteCall 调远端。

### 4.1 初始化

```cpp
meowpapi::PlaceholderApi& papi = meowpapi::PlaceholderApi::getInstance();
papi.initAsServer();   // 服务端模式（ensureLoaded 的 autoInit 已调用，勿重复）
papi.initAsClient();   // 客户端模式：翻译经 RemoteCall 转发远端注册表
```

> 消费者插件经 `ensureLoaded()` 加载后默认已是服务端模式；
> 仅当插件明确要作为"远程客户端"时才覆盖为 `initAsClient()`。

### 4.2 翻译与取值

```cpp
// 翻译整串（替换全部 %name% / {name} / ${papi:...}）
std::string translateString(std::string const& str);
std::string translateStringWithPlayer(std::string const& str, Player* player);

// skipRemote=true：跳过 mainThreadOnly 占位符（保留原文，留主线程二遍翻译）
// 仅服务端模式生效；客户端模式忽略该参数
std::string translateStringWithPlayer(std::string const& str, Player* player, bool skipRemote);

// 取单个占位符值（不存在时返回 "{name}" 形式）
std::string getValue(std::string const& name);
std::string getValueWithPlayer(std::string const& name, Player* player);

// 检查存在性
bool hasPlaceholder(std::string const& name);
```

### 4.3 注册（普通占位符）

回调统一签名为 `std::function<std::string(Player*)>`，服务器级回调收到 `nullptr`：

```cpp
using PlaceholderCallback = std::function<std::string(Player* player)>;

bool registerServerPlaceholder(pluginName, name, cb);                     // 每次求值实时调用
bool registerPlayerPlaceholder(pluginName, name, cb);                     // 每次求值实时调用
bool registerStaticPlaceholder(pluginName, name, cb, updateIntervalMs);   // 结果缓存
```

- `pluginName`：注册归属，`unregisterByPlugin` 按此批量注销。
- `name`：占位符名。重复注册返回 `false`。
- `updateIntervalMs`：静态缓存刷新间隔（毫秒），0 表示不缓存。
- 客户端模式下注册经 RemoteCall 转发到远端注册表。

### 4.4 注册（带参占位符，GMLIB 兼容）

```cpp
using PlaceholderParamCallback =
    std::function<std::string(Player* player, std::string const& paramsJson)>;

bool registerServerPlaceholderWithParams(pluginName, name, cb);
bool registerPlayerPlaceholderWithParams(pluginName, name, cb);
```

`name` 可含 `<param>` 槽位模板；`paramsJson` 为参数 JSON 对象字符串，
每个参数**双键注入**（`{"score":"zxsc","<score>":"zxsc",...}`，
兼容 `params.score` 与 GMLIB 原版 `params['<score>']` 两种取值）。详见 §7。

### 4.5 ABI 查询

```cpp
uint32_t getAbiVersion();        // DLL 侧=编译期宏；wrapper 侧=运行时 DLL 报告值（旧 DLL 返回 0）
bool     isParamPapiSupported(); // 是否支持带参占位符（wrapper 侧查功能位，旧 DLL 返回 false）
```

---

## 5. C++ API（PlaceholderRegistry 直用）

头文件：`meowpapi/PlaceholderRegistry.h`。需要更细控制（线程标记、按插件注销、
列表查询）时直接使用注册表单例：

```cpp
auto& reg = meowpapi::PlaceholderRegistry::getInstance();
```

### 5.1 Remote 版本注册（mainThreadOnly 标记）

回调访问非线程安全 API（Level / Player 内部状态等）时，必须用 `*Remote` 版本注册，
后台线程翻译时自动跳过，留给主线程二遍翻译（见 §10）：

```cpp
bool registerServerPlaceholderRemote(pluginName, name, cb);
bool registerPlayerPlaceholderRemote(pluginName, name, cb);
bool registerStaticPlaceholderRemote(pluginName, name, cb, updateIntervalMs);

// 带参 Remote 版本
bool registerServerPlaceholderWithParamsRemote(pluginName, name, cb);
bool registerPlayerPlaceholderWithParamsRemote(pluginName, name, cb);
```

### 5.2 注销与列表

```cpp
bool                     unregisterPlaceholder(std::string const& name);
void                     unregisterByPlugin(std::string const& pluginName);
void                     clear();                                     // 清空全部（慎用）
std::vector<std::string> listPlaceholders();
std::vector<std::string> listPlaceholdersByPlugin(std::string const& pluginName);
```

### 5.3 翻译 / 取值（与 PlaceholderApi 等价）

```cpp
std::string translateString(str);
std::string translateStringWithPlayer(str, player);
std::string translateStringWithPlayer(str, player, skipRemote);
std::string getValue(name);
std::string getValueWithPlayer(name, player);
bool        hasPlaceholder(name);
```

### 5.4 EnsureLoaded（meowpapi/EnsureLoaded.h）

```cpp
bool meowpapi::ensureLoaded();  // 确保 DLL 已部署+初始化；线程安全，可多次调用
bool meowpapi::isDeployer();    // 本插件是否为部署者（第一个触发部署的插件）

//===== 运行时 ABI 查询（须在 ensureLoaded() 之后调用）=====
uint32_t meowpapi::loadedAbiVersion();       // 运行时 DLL 的 ABI 版本（旧 DLL 返回 0）
uint32_t meowpapi::loadedAbiFeatures();      // 运行时 DLL 的功能位掩码（旧 DLL 返回 0）
bool     meowpapi::paramPapiSupported();     // 便捷判断：是否支持带参占位符
uint64_t meowpapi::loadedBuildTimestamp();   // 运行时 DLL 构建时间戳（PE TimeDateStamp）
```

运行时 ABI 诊断示例（MeowSidebar 同款，enable 阶段打印一行即可秒判部署版本）：

```cpp
uint32_t rtAbi = meowpapi::loadedAbiVersion();
logger.info("MeowPAPI 运行时: ABI 0x{:06x}{} (编译期 0x{:06x})",
    rtAbi, meowpapi::paramPapiSupported() ? " 带参PAPI" : "", MEOWPAPI_ABI_VERSION);
if (rtAbi > 0 && rtAbi < MEOWPAPI_ABI_VERSION) {
    logger.warn("运行中的 MeowPAPI 低于编译期版本，部分功能降级");
}
```

生命周期约定：

- `load()`：调 `ensureLoaded()`。
- `enable()`：直接使用 API。
- `disable()`：只清理自己的 RemoteCall 命名空间
  （`RemoteCall::removeNameSpace("MyPlugin")`）；仅部署者额外调用
  `removeRemoteCallApi()` / `removeBepApiFallback()`。

---

## 6. LSE (JS) API

MeowPAPI 通过 RemoteCall 导出命名空间 `"MeowPAPI"`，全部 14 个函数：

### 6.1 注册占位符

注册流程固定两步：先 `ll.export` 导出回调，再 `ll.import` 注册。

```js
// ===== 服务器级（回调签名：() -> string）=====
ll.export(() => { return "Hello"; }, "MyScript", "getHello");
ll.import("MeowPAPI", "registerServerPlaceholder")("MyScript", "my_hello", "MyScript", "getHello");

// ===== 玩家级（回调签名：(player) -> string）=====
ll.export((player) => { return player.name + " 的称号"; }, "MyScript", "getTitle");
ll.import("MeowPAPI", "registerPlayerPlaceholder")("MyScript", "my_title", "MyScript", "getTitle");

// ===== 静态缓存级（回调签名：() -> string，结果按间隔缓存）=====
ll.import("MeowPAPI", "registerStaticPlaceholder")(
    "MyScript", "my_stat", "MyScript", "getStat", 5000);  // updateIntervalMs=5000

// ===== 带参服务器级（回调签名：(paramsJson: string) -> string）=====
ll.export((paramsJson) => {
    const params = JSON.parse(paramsJson);   // {"score":"zxsc","number":"10"}
    return "查询 " + params.score + " 榜前 " + params.number;
}, "MyScript", "getRank");
ll.import("MeowPAPI", "registerServerPlaceholderWithParams")(
    "MyScript", "title_score_rank_<score>_<title>_<number>", "MyScript", "getRank");

// ===== 带参玩家级（回调签名：(player, paramsJson: string) -> string）=====
ll.export((player, paramsJson) => {
    const params = JSON.parse(paramsJson);
    return player.name + ": " + params.kind;
}, "MyScript", "getInfo");
ll.import("MeowPAPI", "registerPlayerPlaceholderWithParams")(
    "MyScript", "player_info_<kind>", "MyScript", "getInfo");
```

注册参数（四个注册函数通用）：

| 参数 | 含义 |
|------|------|
| `pluginName` | 注册归属插件名（按插件批量注销的依据） |
| `papiName` | 占位符名，带参版可含 `<param>` 槽位模板 |
| `callbackPluginName` | 回调所属 RemoteCall 命名空间（即 `ll.export` 的第二参数） |
| `callbackFuncName` | 回调函数名（即 `ll.export` 的第三参数） |

> LSE 回调目标函数被卸载（如脚本插件热重载/关服）时静默返回空串，
> 目标恢复后占位符自动继续工作，不产生报错日志。

### 6.2 查询与翻译

```js
// 翻译
ll.import("MeowPAPI", "translateString")("TPS: {server_tps}");
ll.import("MeowPAPI", "translateStringWithPlayer")("{pl.realName} 血量 {pl.health}", "Steve");

// 取值
ll.import("MeowPAPI", "GetValue")("server_tps");
ll.import("MeowPAPI", "GetValueWithPlayer")("pl.ping", "Steve");

// 检查 / 列表
ll.import("MeowPAPI", "hasPlaceholder")("server_tps");       // -> bool
ll.import("MeowPAPI", "listPlaceholders")();                 // -> JSON 数组字符串

// 注销
ll.import("MeowPAPI", "unRegisterPlaceholder")("my_title");  // -> bool
```

### 6.3 ABI 与版本查询

```js
const abi = ll.import("MeowPAPI", "getAbiVersion")();        // 0x010100 = 65792
const feat = ll.import("MeowPAPI", "getAbiFeatures")();      // 功能位掩码
if (feat & 1) { /* 支持带参占位符（bit0 = MEOWPAPI_ABI_FEATURE_PARAMS） */ }
const ver = ll.import("MeowPAPI", "getVersion")();           // "1.1.0"（版本字符串）
```

---

## 7. 带参占位符（GMLIB PAPI 兼容）

### 7.1 翻译语法

```
${papi:<占位符名>[,key=value[,key=value...]]}
```

完整示例（对应 §6.1 注册的 `title_score_rank_<score>_<title>_<number>`）：

```
${papi:title_score_rank_<score>_<title>_<number>,<score>=zxsc,<title>=在线榜,<number>=10}
```

回调收到 `paramsJson`（**键双格式注入**——每个参数同时以带尖括号键与剥尖括号键存在）：

```json
{"score":"zxsc","title":"在线榜","number":"10","<score>":"zxsc","<title>":"在线榜","<number>":"10"}
```

> 双键设计兼容两种取值写法：`params.score`（剥尖括号）与 `params['<score>']`
> （GMLIB 原版语义）。从 GMLIB 迁移的 JS 插件（回调里 `params['<number>']`）
> 无需改动即可工作。

### 7.2 槽位模板匹配

注册名含 `<param>` 时，翻译按 `_` 分段匹配实际占位符名提取参数：

```
注册: title_score_rank_<score>_<title>_<number>
使用: ${papi:title_score_rank_zxsc_在线榜_10}
     → 槽位提取 score=zxsc, title=在线榜, number=10
```

**参数合并优先级：显式参数 > 槽位参数**。两者同时存在时同名键以显式参数为准：

```
${papi:title_score_rank_zxsc_在线榜_10,<number>=5}
     → score/title 来自槽位，number=5 来自显式参数
```

### 7.3 转义与嵌套

| 能力 | 写法 | 说明 |
|------|------|------|
| 逗号转义 | `\,` | 参数值中包含字面逗号 |
| 等号转义 | `\=` | 参数值中包含字面等号 |
| 值嵌套 | `${papi:a,<k>=${papi:b,...}}` | 值内嵌套表达式先递归翻译再作为参数值 |
| 嵌套简单占位符 | `${papi:a,<k>={server_tps}}` | 嵌套非 papi 表达式按普通占位符解析 |
| key 规范化 | `<score>` → `score` | 参数键自动去除 `<` `>` |

嵌套未命中时该嵌套表达式保留原文。非 `papi:` 前缀的 `${...}` 不被拦截，
按原有 `{name}` 逻辑处理。

### 7.4 无参引用带参占位符

`%name%` / `{name}` 直接引用带参注册的占位符时走**空参数对象** `{}`，
槽位模板匹配照常提取参数（无需显式传参即可用槽位值）：

```
注册: player_info_<kind>
使用: {player_info_gold}   → 槽位提取 kind=gold，回调收到 {"kind":"gold"}
```

> 勿在回调里假设 paramsJson 一定含特定键——`%name%` 无槽位引用时是 `{}`。

### 7.5 通道汇总

带参注册共四个通道，行为一致：

| 通道 | 入口 |
|------|------|
| C++ 静态库 | `PlaceholderApi::registerServer/PlayerPlaceholderWithParams` |
| LSE | `ll.import("MeowPAPI", "registerServer/PlayerPlaceholderWithParams")` |
| C ABI | `MeowPAPI_RegisterPlaceholderWithParams`（type: 0=Server / 1=Player） |
| DLL 内部 | `PlaceholderRegistry::register*WithParams[Remote]` |

---

## 8. ABI 版本与功能位

### 8.1 单一来源宏（include/meowpapi/DllExports.h）

```cpp
#define MEOWPAPI_V_MAJOR 1
#define MEOWPAPI_V_MINOR 1
#define MEOWPAPI_V_PATCH 0
#define MEOWPAPI_ABI_VERSION  ((MAJOR<<16)|(MINOR<<8)|PATCH)   // 0x010100
#define MEOWPAPI_VERSION_STRING ...                            // "1.1.0"

// 功能位掩码（add-only：只增位不改值）
#define MEOWPAPI_ABI_FEATURE_PARAMS 0x00000001   // 带参占位符
```

### 8.2 查询通道

| 通道 | 调用 | 返回 |
|------|------|------|
| 编译期宏 | `MEOWPAPI_ABI_VERSION` | 消费者编译时的版本 |
| C API | `MeowPAPI_GetAbiVersion()` / `MeowPAPI_GetAbiFeatures()` | DLL 运行时值 |
| C++ | `PlaceholderApi::getAbiVersion()` / `isParamPapiSupported()` | 见 §4.5 |
| C++ | `meowpapi::loadedAbiVersion()` 等（EnsureLoaded.h） | 见 §5.4 |
| LSE | `ll.import("MeowPAPI","getAbiVersion")()` / `getAbiFeatures` / `getVersion` | 同 C API |
| 游戏内 | `/meowpapi version` | 见 §14 |

另有 `MeowPAPI_GetBuildTimestamp()`：返回 PE TimeDateStamp（Unix 秒），
用于检测已部署 DLL 是否比嵌入版本旧。

### 8.3 服务器启动时的自动版本广播

MeowPAPI 监听 `ServerStartedEvent`，开服完成时在日志自动广播一行版本摘要：

```
[MeowPAPI] MeowPAPI v1.1.0 (ABI 0x010100, build 2026-08-24 18:20) 已就绪: 42 个占位符, lrca已挂载(LSE可用)
```

无需任何配置。与磁盘 DLL 的 LastWriteTime 对比即可秒判实际部署版本
（同 HologramLib 构建时间戳模式）。

### 8.4 兼容性判断（推荐写法）

```cpp
// 运行时 DLL 是否支持带参功能：功能位 + 导出符号双重判定（旧 DLL 自动降级）
meowpapi::PlaceholderApi::getInstance().isParamPapiSupported();
```

```js
const feat = ll.import("MeowPAPI", "getAbiFeatures")();
const ok = (feat & 1) !== 0;   // bit0 = 带参
```

### 8.4 版本历史

| ABI | 版本 | 功能 |
|-----|------|------|
| 0x010000 | 1.0.0 | 注册 / 翻译 / 静态缓存 / RemoteCall 桥 |
| 0x010100 | 1.1.0 | 带参占位符（GMLIB PAPI 兼容）、ABI 查询导出 |

旧版 DLL（<1.1.0）无 `GetAbiVersion` 等导出，`GetProcAddress` 返回空 →
视为版本 0 / 无功能位，消费者据此降级。

---

## 9. 内置占位符全表

内置占位符在 `ensureLoaded()` 自动初始化时注册（移植自 CoralFans / BetterSidebar），
注册归属插件名为 `MeowSidebar`（历史原因）。**MT** = 仅主线程（mainThreadOnly）。

### 9.1 服务器级（Static 缓存）

| 占位符 | 含义 | 缓存 | 线程 |
|--------|------|------|------|
| `server_tps` | 服务器 TPS | 500ms | MT |
| `server_mspt` | 每刻毫秒数 | 500ms | MT |
| `server_online` | 当前在线人数 | 500ms | MT |
| `server_max_players` | 最大玩家数 | 5000ms | MT |
| `server_version` | 游戏版本 | 5000ms | 任意 |
| `server_protocol_version` | 协议版本号 | 5000ms | 任意 |
| `server_world_name` | 世界名 | 5000ms | MT |
| `server_difficulty` | 难度 | 5000ms | MT |
| `server_uptime` | 运行时长 | 1000ms | 任意 |
| `server_ram_used` | 已用内存 | 1000ms | 任意 |
| `server_ram_free` | 可用内存 | 1000ms | 任意 |
| `server_ram_max` | 总内存 | 5000ms | 任意 |
| `server_ram_bds_used` | BDS 进程内存 | 1000ms | 任意 |
| `server_total_entities` | 已加载实体总数（玩家/生物/掉落物/弹射物等） | 1000ms | MT |
| `server_total_chunks` | 已加载区块数 | 5000ms | 任意 |
| `server_name` | 服务器名（server.properties） | 5000ms | 任意 |
| `server_port` | IPv4 端口 | 5000ms | MT |
| `server_port_v6` | IPv6 端口 | 5000ms | MT |
| `server_on_allowlist` | 是否启用白名单 | 5000ms | MT |
| `server_has_whitelist` | 是否存在白名单文件 | 5000ms | MT |

### 9.2 玩家级（全部 MT）

| 占位符 | 含义 |
|--------|------|
| `pl.realName` | 玩家名 |
| `pl.health` / `pl.maxHealth` | 当前 / 最大生命值 |
| `pl.hunger` | 饥饿值 |
| `pl.pos` | 坐标（x,y,z） |
| `pl.posX` / `pl.posY` / `pl.posZ` | 单轴坐标 |
| `pl.dimId` / `pl.dimName` | 维度 ID / 名称 |
| `pl.expLevel` | 经验等级 |
| `pl.gamemode` | 游戏模式 |
| `pl.flying` | 是否飞行 |
| `pl.isOp` | 是否 OP |
| `pl.device` | 设备类型 |
| `pl.ping` | 延迟（毫秒） |
| `pl.ip` | IP 地址 |
| `pl.uuid` / `pl.xuid` | UUID / XUID |
| `pl.handItem` | 手持物品名 |
| `pl.speed` | 移动速度 |
| `pl.direction` | 朝向 |
| `pl.money` | LLMoney 余额（未装返回 0） |
| `pl.money_formatted` | LLMoney 余额（千分位格式） |

### 9.3 时间级（Static 缓存 1000ms，任意线程）

| 占位符 | 含义 |
|--------|------|
| `date.h` / `date.m` / `date.s` | 时 / 分 / 秒 |
| `date.D` / `date.M` / `date.Y` | 日 / 月 / 年 |
| `date.W` | 星期 |

---

## 10. 线程模型

### 10.1 mainThreadOnly 机制

访问非线程安全 API（Level / ProfilerLite / NetworkHandler / Player 内部状态）的
占位符回调必须以 `*Remote` 版本注册（`mainThreadOnly=true`）。后台线程翻译时
这些占位符被跳过并保留原文。

### 10.2 两遍翻译（skipRemote）

```
后台线程: translateStringWithPlayer(str, player, /*skipRemote=*/true)
          → mainThreadOnly 占位符保留原文
主线程:   translateStringWithPlayer(str, player, /*skipRemote=*/false)
          → 全量翻译（含 mainThreadOnly 占位符）
```

典型消费者（MeowSidebar）：后台线程定期刷新非敏感占位符，主线程 flush 时
补全玩家级 / TPS 等敏感占位符——既不阻塞又避免数据竞争拖慢 TPS。

### 10.3 线程安全总结

| 操作 | 线程安全性 |
|------|-----------|
| `ensureLoaded()` / `isDeployer()` | 线程安全，可并发 |
| 注册 / 注销 | 应在插件生命周期回调（主线程）执行 |
| 翻译 / 取值 | 可跨线程；`mainThreadOnly` 占位符需主线程补翻 |
| LSE 注册的占位符 | 一律 mainThreadOnly（LSE 引擎单线程） |

---

## 11. C ABI 参考（DllExports）

`MeowPAPI.dll` 导出的 C 接口。**通常只有静态库 wrapper 使用**；消费者一律走
C++ / LSE 层。完整列表见 `include/meowpapi/DllExports.h`，速查：

```c
// 版本
const char* MeowPAPI_GetVersion(void);
uint32_t    MeowPAPI_GetAbiVersion(void);
uint32_t    MeowPAPI_GetAbiFeatures(void);
uint64_t    MeowPAPI_GetBuildTimestamp(void);

// 初始化（autoInit 已含，勿手动调用）
void MeowPAPI_InitAsServer(void);
void MeowPAPI_InitAsClient(void);

// 回调调用器（wrapper 内部使用）
void MeowPAPI_SetCallbackInvoker(MeowPAPI_CallbackFn fn);
void MeowPAPI_SetCallbackInvokerWithParams(MeowPAPI_CallbackWithParamsFn fn);

// 翻译 / 取值 / 检查（返回结果长度；out=NULL 时返回所需长度）
int  MeowPAPI_TranslateString(const char* str, char* out, int outSize);
int  MeowPAPI_TranslateStringWithPlayer(const char* str, void* player, char* out, int outSize);
int  MeowPAPI_TranslateStringWithPlayerSkip(const char* str, void* player, int skipRemote, char* out, int outSize);
int  MeowPAPI_GetValue(const char* name, char* out, int outSize);
int  MeowPAPI_GetValueWithPlayer(const char* name, void* player, char* out, int outSize);
int  MeowPAPI_HasPlaceholder(const char* name);

// 注册（type: 0=Server 1=Player 2=Static；返回 1=成功）
int  MeowPAPI_RegisterPlaceholder(const char* pluginName, const char* name,
                                  int type, uint64_t callbackId, int updateIntervalMs);
int  MeowPAPI_RegisterPlaceholderWithParams(const char* pluginName, const char* name,
                                            int type, uint64_t callbackId, int updateIntervalMs);
int  MeowPAPI_UnregisterPlaceholder(const char* name);
void MeowPAPI_UnregisterByPlugin(const char* pluginName);
void MeowPAPI_Clear(void);

// 列表（JSON 数组字符串）
int  MeowPAPI_ListPlaceholders(char* out, int outSize);
int  MeowPAPI_ListPlaceholdersByPlugin(const char* pluginName, char* out, int outSize);

// RemoteCall / 内置 / BEPAPI（autoInit 已含）
void MeowPAPI_ExportRemoteCallApi(void);
void MeowPAPI_RemoveRemoteCallApi(void);
void MeowPAPI_RegisterBuiltinPlaceholders(void);
int  MeowPAPI_IsBepApiAvailable(void);
void MeowPAPI_InstallBepApiFallback(void);
void MeowPAPI_RemoveBepApiFallback(void);

// 远程客户端桥接
int  MeowPAPI_IsRemoteAvailable(void);
int  MeowPAPI_RemoteTranslateString(const char* str, char* out, int outSize);
int  MeowPAPI_RemoteTranslateStringWithPlayer(const char* str, const char* playerName, char* out, int outSize);
int  MeowPAPI_RemoteGetValue(const char* name, char* out, int outSize);
int  MeowPAPI_RemoteGetValueWithPlayer(const char* name, const char* playerName, char* out, int outSize);
int  MeowPAPI_RemoteHasPlaceholder(const char* name);
```

约束：单次结果最大 `MEOWPAPI_MAX_RESULT`（8192）字节。

---

## 12. RemoteCall 代理（RemoteCallAPI.h）

消费者插件需要 `RemoteCall::exportAs / importAs` 能力但**不想链接
legacyremotecall** 时，用 MeowPAPI 头文件替代原版：

```cpp
// 替代 #include <RemoteCallAPI.h>
#include "meowpapi/RemoteCallAPI.h"

// 用法与原版完全一致：
RemoteCall::exportAs("MyPlugin", "myFunc", []() -> bool { return true; });
auto fn = RemoteCall::importAs<int(std::string const&)>("NS", "func");
```

机制：编译消费者插件时（未定义 `MEOWPAPI_DLL_EXPORTS`）走**代理模式**——
`exportAs / importAs` 等经函数指针转发到 MeowPAPI.dll 内的 `RC_*` 代理，
由 MeowPAPI.dll 统一与 lrca 通信。函数指针在 `ensureLoaded()` 时经
`GetProcAddress` 初始化。调用顺序要求：先 `ensureLoaded()` 再使用
RemoteCall 代理。

---

## 13. 错误处理与常见问题

### 13.1 输出标记

| 输出 | 含义 | 处理建议 |
|------|------|----------|
| `{name}` 原样保留 | 占位符未注册，或 mainThreadOnly 被后台线程跳过 | 主线程二遍翻译；检查名字拼写 |
| `{ERR}` | 回调抛异常 / 回调调用器缺失 / 无参引用带参 entry 走了空回调路径 | 检查回调实现；带参注册勿假设必有参数 |
| 空串 | 远程回调目标已被卸载（热重载/关服） | 属正常静默降级，目标恢复后自愈 |

### 13.2 常见问题

**Q：`registerServerPlaceholder` 返回 false？**
同名占位符已存在（内置占位符归属 `MeowSidebar`，见 §9）。换名或先
`unregisterPlaceholder`。

**Q：注册的占位符在侧边栏不刷新？**
静态占位符按 `updateIntervalMs` 缓存；LSE 注册的回调一律
mainThreadOnly，后台线程刷新周期内保留原文属正常现象。

**Q：`%name%` 引用带参占位符输出 `{ERR}`？**
1.1.0 已修复（无参引用自动传 `{}` 走带参路径）。若仍出现，说明运行的是
旧版 DLL——用 `getAbiVersion()` 核实（应 ≥ 0x010100），并重新部署
`plugins/MeowPAPI/MeowPAPI.dll`（构建产物在 `build/windows/x64/release/`，
须复制到插件根目录才算部署）。

**Q：多消费者插件会重复部署 DLL 吗？**
不会。`std::call_once` 保证首个消费者部署一次，其余直接连接已加载实例。

**Q：消费者 disable 时该清理什么？**
只清理自己的 RemoteCall 命名空间（`RemoteCall::removeNameSpace("MyPlugin")`）
和按插件注销占位符（`unregisterByPlugin`）；全局清理仅部署者执行
（`isDeployer()` 判断）。

**Q：消费者只 include 了 RemoteCallAPI.h、没调 ensureLoaded，程序行为异常？**
静态库可能被 `/OPT:REF` 丢弃导致代理指针为空。头文件已用
`#pragma comment(linker, "/include:...")` 强制拉入，但仍应在 `load()` 里
显式调用 `meowpapi::ensureLoaded()` 完成同步初始化。

---

## 14. 游戏内指令（/meowpapi）

MeowPAPI 作为独立插件注册游戏内指令，全部子命令游戏内聊天栏输出：

| 命令 | 权限 | 说明 |
|------|------|------|
| `/meowpapi` | 所有玩家 | 帮助（含版本摘要） |
| `/meowpapi version` | 所有玩家 | 版本信息：版本字符串、ABI、构建时间、功能、lrca 挂载状态、占位符总数 |
| `/meowpapi list [页码]` | 仅 OP（execute 内自检） | 分页浏览占位符列表（每页 8 条，含类型/来源插件/带参标记） |

示例输出：

```
===== MeowPAPI =====
版本: 1.1.0 (ABI 0x010100)
构建: 2026-08-24 18:20
功能: 带参占位符(GMLIB PAPI 兼容)
lrca: 已挂载 (LSE 可用)
占位符: 42 个 (/meowpapi list 查看)
```

```
===== 占位符列表 [1/6]===== 42 个
#1 mspt [Static] MeowSidebar
#2 pl.health [Player] MeowSidebar
...
下一页: /meowpapi list 2
```

> 命令注册为 `CommandPermissionLevel::Any` + execute 内
> `getCommandPermissionLevel() >= 1` 自检 OP（避免 Bedrock 客户端对
> GameDirectors 命令的本地隐藏问题，MeowMenu 同款方案）。
> `version` 对所有玩家开放（版本信息无害），`list` 仅 OP。
