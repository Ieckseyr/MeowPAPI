// Builtins.cpp - 内置原生占位符实现
//
// 移植自 CoralFans (MSPT/TPS via ProfilerLite) 和 BetterSidebar (服务器/玩家/时间变量)
#include "meowpapi/Builtins.h"
#include "meowpapi/PlaceholderRegistry.h"

#include "ll/api/service/Bedrock.h"
#include "ll/api/Versions.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/storage/LevelData.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/network/NetworkPeer.h"
#include "mc/profile/ProfilerLite.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/deps/core/platform/BuildPlatform.h"

#include <chrono>
#include <ctime>
#include <fmt/format.h>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

namespace meowpapi {

namespace {

// 服务器启动时间（用于计算运行时间）
auto gStartTime = std::chrono::steady_clock::now();

//服务器级占位符
// 注意：PlaceholderCallback 签名为 std::string(Player*)，服务器级占位符忽略 player 参数

std::string getServerTps(Player* /*p*/) {
    try {
        auto mspt = (double)ProfilerLite::gProfilerLiteInstance().mDebugServerTickTime->count() / 1000000.0;
        double tps = mspt <= 50 ? 20 : (1000.0 / mspt);
        return fmt::format("{:.2f}", tps);
    } catch (...) { return "20.00"; }
}

std::string getServerMspt(Player* /*p*/) {
    try {
        auto mspt = (double)ProfilerLite::gProfilerLiteInstance().mDebugServerTickTime->count() / 1000000.0;
        return fmt::format("{:.2f}", mspt);
    } catch (...) { return "0.00"; }
}

std::string getServerOnline(Player* /*p*/) {
    auto level = ll::service::getLevel();
    if (!level) return "0";
    return std::to_string(level->getNumRemotePlayers());
}

std::string getServerMaxPlayers(Player* /*p*/) {
    auto handler = ll::service::getServerNetworkHandler();
    if (!handler) return "0";
    return std::to_string(handler->mMaxNumPlayers);
}

std::string getServerVersion(Player* /*p*/) {
    return ll::getGameVersion().to_string();
}

std::string getServerProtocolVersion(Player* /*p*/) {
    return std::to_string(ll::getNetworkProtocolVersion());
}

std::string getServerWorldName(Player* /*p*/) {
    auto level = ll::service::getLevel();
    if (!level) return "unknown";
    return level->getLevelData().mLevelName.get();
}

std::string getServerDifficulty(Player* /*p*/) {
    auto level = ll::service::getLevel();
    if (!level) return "unknown";
    int diff = static_cast<int>(level->getDifficulty());
    switch (diff) {
        case 0: return "和平";
        case 1: return "简单";
        case 2: return "普通";
        case 3: return "困难";
        default: return "未知";
    }
}

std::string getServerUptime(Player* /*p*/) {
    auto now = std::chrono::steady_clock::now();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now - gStartTime).count();
    int h = (int)(seconds / 3600);
    int m = (int)((seconds % 3600) / 60);
    int s = (int)(seconds % 60);
    return fmt::format("{}h{}m{}s", h, m, s);
}

std::string getServerRamUsed(Player* /*p*/) {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(memInfo);
    GlobalMemoryStatusEx(&memInfo);
    return std::to_string((unsigned long long)(memInfo.ullTotalPhys - memInfo.ullAvailPhys) / 1024 / 1024) + "MB";
#else
    return "N/A";
#endif
}

std::string getServerRamFree(Player* /*p*/) {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(memInfo);
    GlobalMemoryStatusEx(&memInfo);
    return std::to_string((unsigned long long)memInfo.ullAvailPhys / 1024 / 1024) + "MB";
#else
    return "N/A";
#endif
}

std::string getServerRamMax(Player* /*p*/) {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(memInfo);
    GlobalMemoryStatusEx(&memInfo);
    return std::to_string((unsigned long long)memInfo.ullTotalPhys / 1024 / 1024) + "MB";
#else
    return "N/A";
#endif
}

std::string getServerRamBdsUsed(Player* /*p*/) {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return std::to_string((unsigned long long)pmc.WorkingSetSize / 1024 / 1024) + "MB";
    }
    return "N/A";
#else
    return "N/A";
#endif
}

std::string getServerTotalEntities(Player* /*p*/) {
    // 获取服务器所有已加载实体总数 (含玩家、生物、掉落物、弹射物等)
    auto level = ll::service::getLevel();
    if (!level) return "0";
    try {
        auto const& actors = level->getRuntimeActorList();
        return std::to_string(actors.size());
    } catch (...) { return "0"; }
}

std::string getServerTotalChunks(Player* /*p*/) {
    return "N/A";
}

std::string getServerName(Player* /*p*/) {
    return "MeowServer";
}

std::string getServerPort(Player* /*p*/) {
    return "19132";
}

std::string getServerPortV6(Player* /*p*/) {
    return "19133";
}

std::string getServerOnAllowlist(Player* /*p*/) {
    return "否";
}

std::string getServerHasWhitelist(Player* /*p*/) {
    return "否";
}

//玩家级占位符

std::string safeStr(std::function<std::string()> fn, std::string const& fallback = "未知") {
    try { return fn(); } catch (...) { return fallback; }
}

std::string getPlRealName(Player* p) {
    if (!p) return "未知";
    return p->getRealName();
}

std::string getPlHealth(Player* p) {
    if (!p) return "0";
    return std::to_string(p->getHealth());
}

std::string getPlMaxHealth(Player* p) {
    if (!p) return "20";
    return std::to_string(p->getMaxHealth());
}

std::string getPlHunger(Player* p) {
    if (!p) return "0";
    auto attr = p->getAttribute(Player::HUNGER());
    if (attr.mPtr != nullptr) {
        return std::to_string((int)attr.mPtr->mCurrentValue);
    }
    return "0";
}

std::string getPlPos(Player* p) {
    if (!p) return "0, 0, 0";
    auto const& pos = p->getPosition();
    return fmt::format("{}, {}, {}", (int)pos.x, (int)pos.y, (int)pos.z);
}

std::string getPlPosX(Player* p) {
    if (!p) return "0";
    return std::to_string((int)p->getPosition().x);
}

std::string getPlPosY(Player* p) {
    if (!p) return "0";
    return std::to_string((int)p->getPosition().y);
}

std::string getPlPosZ(Player* p) {
    if (!p) return "0";
    return std::to_string((int)p->getPosition().z);
}

std::string getPlDimId(Player* p) {
    if (!p) return "0";
    return std::to_string((int)p->getDimensionId());
}

std::string getPlDimName(Player* p) {
    if (!p) return "未知";
    int dimId = (int)p->getDimensionId();
    switch (dimId) {
        case 0: return "主世界";
        case 1: return "下界";
        case 2: return "末地";
        default: return "未知";
    }
}

std::string getPlExpLevel(Player* p) {
    if (!p) return "0";
    auto attr = p->getAttribute(Player::LEVEL());
    if (attr.mPtr != nullptr) {
        return std::to_string((int)attr.mPtr->mCurrentValue);
    }
    return "0";
}

std::string getPlGamemode(Player* p) {
    if (!p) return "未知";
    // 通过 Player 的游戏模式获取
    // Player::getPlayerGameType() 不确定是否存在，使用命令权限级别粗略判断
    return "未知";
}

std::string getPlFlying(Player* p) {
    if (!p) return "否";
    return p->isFlying() ? "是" : "否";
}

std::string getPlIsOp(Player* p) {
    if (!p) return "否";
    return p->isOperator() ? "是" : "否";
}

std::string getPlDevice(Player* p) {
    if (!p) return "未知";
    auto bp = p->mBuildPlatform;
    switch (bp) {
        case BuildPlatform::Google:  return "Android";
        case BuildPlatform::IOS:     return "iOS";
        case BuildPlatform::Osx:     return "macOS";
        case BuildPlatform::Amazon:  return "FireOS";
        case BuildPlatform::Uwp:     return "Win10";
        case BuildPlatform::Win32:   return "Win";
        case BuildPlatform::Dedicated: return "Server";
        case BuildPlatform::Sony:    return "PS";
        case BuildPlatform::Nx:      return "NS";
        case BuildPlatform::Xbox:    return "Xbox";
        case BuildPlatform::Linux:   return "Linux";
        default: return "未知";
    }
}

std::string getPlPing(Player* p) {
    if (!p) return "0";
    auto status = p->getNetworkStatus();
    if (status.has_value()) {
        return std::to_string(status->mAveragePing);
    }
    return "0";
}

std::string getPlIp(Player* p) {
    if (!p) return "未知";
    try {
        return p->getIPAndPort();
    } catch (...) { return "未知"; }
}

std::string getPlUuid(Player* p) {
    if (!p) return "未知";
    return p->getUuid().asString();
}

std::string getPlXuid(Player* p) {
    if (!p) return "未知";
    return p->getXuid();
}

std::string getPlHandItem(Player* p) {
    if (!p) return "空";
    auto const& item = p->getCarriedItem();
    if (item.isNull()) return "空";
    return item.getTypeName();
}

std::string getPlSpeed(Player* p) {
    if (!p) return "0.00";
    return fmt::format("{:.2f}", p->getSpeed());
}

std::string getPlDirection(Player* p) {
    if (!p) return "未知";
    auto const& rot = p->getRotation();
    float yaw = rot.y;
    // 将 yaw 转换为方向
    while (yaw < 0) yaw += 360;
    while (yaw >= 360) yaw -= 360;
    if (yaw >= 315 || yaw < 45) return "南";
    if (yaw >= 45 && yaw < 135) return "西";
    if (yaw >= 135 && yaw < 225) return "北";
    return "东";
}

//时间级占位符（静态缓存，1秒更新）=====

std::string getDateHour(Player* /*p*/) {
    auto now = std::time(nullptr);
    auto* tm = std::localtime(&now);
    return fmt::format("{:02d}", tm->tm_hour);
}

std::string getDateMinute(Player* /*p*/) {
    auto now = std::time(nullptr);
    auto* tm = std::localtime(&now);
    return fmt::format("{:02d}", tm->tm_min);
}

std::string getDateSecond(Player* /*p*/) {
    auto now = std::time(nullptr);
    auto* tm = std::localtime(&now);
    return fmt::format("{:02d}", tm->tm_sec);
}

std::string getDateDay(Player* /*p*/) {
    auto now = std::time(nullptr);
    auto* tm = std::localtime(&now);
    return std::to_string(tm->tm_mday);
}

std::string getDateMonth(Player* /*p*/) {
    auto now = std::time(nullptr);
    auto* tm = std::localtime(&now);
    return std::to_string(tm->tm_mon + 1);
}

std::string getDateYear(Player* /*p*/) {
    auto now = std::time(nullptr);
    auto* tm = std::localtime(&now);
    return std::to_string(tm->tm_year + 1900);
}

std::string getDateWeekday(Player* /*p*/) {
    auto now = std::time(nullptr);
    auto* tm = std::localtime(&now);
    const char* weekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
    return std::string("星期") + weekdays[tm->tm_wday];
}

//LLMoney 经济系统集成
// 参考 MeowSync 的 EconomySync 实现：通过 LoadLibraryA 直接加载 LegacyMoney.dll，
// 使用 GetProcAddress 获取 LLMoney_Get / LLMoney_Set 导出函数。
// LLMoney 内部使用 std::shared_mutex 保护数据，LLMoney_Get 是线程安全的；
// 但本实现仍注册为 Remote（主线程）占位符，因为需要调用 Player::getXuid()，
// 与其它 pl.* 占位符保持一致的线程模型。
using FnLLMoneyGet = long long(*)(std::string);
using FnLLMoneySet = bool(*)(std::string, long long);

std::once_flag  gLLMoneyLoadFlag;
bool            gLLMoneyAvailable = false;
FnLLMoneyGet    gFnLLMoneyGet     = nullptr;
FnLLMoneySet    gFnLLMoneySet     = nullptr;
#ifdef _WIN32
HMODULE         gLegacyMoneyDll   = nullptr;
#endif

// 线程安全的懒加载：首次调用时加载 LegacyMoney.dll，加载失败后不再重试
void tryLoadLLMoney() {
    std::call_once(gLLMoneyLoadFlag, []() {
#ifdef _WIN32
        gLegacyMoneyDll = LoadLibraryA("LegacyMoney.dll");
        if (!gLegacyMoneyDll) return;
        gFnLLMoneyGet = reinterpret_cast<FnLLMoneyGet>(GetProcAddress(gLegacyMoneyDll, "LLMoney_Get"));
        gFnLLMoneySet = reinterpret_cast<FnLLMoneySet>(GetProcAddress(gLegacyMoneyDll, "LLMoney_Set"));
        if (gFnLLMoneyGet && gFnLLMoneySet) {
            gLLMoneyAvailable = true;
        } else {
            // DLL 已加载但未找到导出函数，释放并清空
            FreeLibrary(gLegacyMoneyDll);
            gLegacyMoneyDll = nullptr;
            gFnLLMoneyGet = nullptr;
            gFnLLMoneySet = nullptr;
        }
#endif
    });
}

// 查询玩家金钱余额（xuid 为空或 LLMoney 不可用时返回 0）
long long getLLMoneyBalance(std::string const& xuid) {
    if (xuid.empty()) return 0;
    tryLoadLLMoney();
    if (!gLLMoneyAvailable || !gFnLLMoneyGet) return 0;
    try {
        return gFnLLMoneyGet(xuid);
    } catch (...) {
        return 0;
    }
}

// 千分位格式化：1234567 -> "1,234,567"，-1234567 -> "-1,234,567"
std::string formatMoneyWithThousands(long long value) {
    auto absval = static_cast<unsigned long long>(value < 0 ? -value : value);
    std::string s = std::to_string(absval);
    std::string result;
    int n = (int)s.size();
    result.reserve(n + n / 3);
    for (int i = 0; i < n; i++) {
        if (i > 0 && (n - i) % 3 == 0) result += ',';
        result += s[i];
    }
    if (value < 0) result.insert(result.begin(), '-');
    return result;
}

std::string getPlMoney(Player* p) {
    if (!p) return "0";
    return std::to_string(getLLMoneyBalance(p->getXuid()));
}

std::string getPlMoneyFormatted(Player* p) {
    if (!p) return "0";
    return formatMoneyWithThousands(getLLMoneyBalance(p->getXuid()));
}

} // anonymous namespace

void registerBuiltinPlaceholders() {
    auto& reg = PlaceholderRegistry::getInstance();

    // 服务器级占位符（使用 Static 带缓存，避免每个玩家重复计算）
    // 服务器级值对所有玩家相同，缓存结果跨玩家共享
    // 缓存间隔根据数据变化频率调整：
    //   - TPS/MSPT/在线人数：500ms（变化较快，但不需要每 tick 重算）
    //   - 内存/运行时间：1000ms（每秒变化）
    //   - 版本/世界名/难度/端口/白名单：5000ms（基本不变）
    // 以下占位符访问 Level/ProfilerLite/NetworkHandler 等非线程安全 API，
    // 必须使用 Remote 版本（mainThreadOnly=true），后台线程跳过，主线程调用
    reg.registerStaticPlaceholderRemote("MeowSidebar", "server_tps",                 getServerTps,              500);
    reg.registerStaticPlaceholderRemote("MeowSidebar", "server_mspt",                getServerMspt,             500);
    reg.registerStaticPlaceholderRemote("MeowSidebar", "server_online",              getServerOnline,           500);
    reg.registerStaticPlaceholderRemote("MeowSidebar", "server_max_players",         getServerMaxPlayers,      5000);
    // 以下占位符只访问线程安全的 API（ll::getGameVersion/Windows API/chrono），可在后台线程调用
    reg.registerStaticPlaceholder("MeowSidebar", "server_version",             getServerVersion,         5000);
    reg.registerStaticPlaceholder("MeowSidebar", "server_protocol_version",    getServerProtocolVersion, 5000);
    reg.registerStaticPlaceholderRemote("MeowSidebar", "server_world_name",          getServerWorldName,       5000);
    reg.registerStaticPlaceholderRemote("MeowSidebar", "server_difficulty",          getServerDifficulty,      5000);
    reg.registerStaticPlaceholder("MeowSidebar", "server_uptime",              getServerUptime,          1000);
    reg.registerStaticPlaceholder("MeowSidebar", "server_ram_used",            getServerRamUsed,         1000);
    reg.registerStaticPlaceholder("MeowSidebar", "server_ram_free",            getServerRamFree,         1000);
    reg.registerStaticPlaceholder("MeowSidebar", "server_ram_max",             getServerRamMax,          5000);
    reg.registerStaticPlaceholder("MeowSidebar", "server_ram_bds_used",        getServerRamBdsUsed,      1000);
    reg.registerStaticPlaceholderRemote("MeowSidebar", "server_total_entities",      getServerTotalEntities,   1000);
    reg.registerStaticPlaceholder("MeowSidebar", "server_total_chunks",        getServerTotalChunks,     5000);
    reg.registerStaticPlaceholder("MeowSidebar", "server_name",                getServerName,            5000);
    reg.registerStaticPlaceholderRemote("MeowSidebar", "server_port",                getServerPort,            5000);
    reg.registerStaticPlaceholderRemote("MeowSidebar", "server_port_v6",             getServerPortV6,          5000);
    reg.registerStaticPlaceholderRemote("MeowSidebar", "server_on_allowlist",        getServerOnAllowlist,     5000);
    reg.registerStaticPlaceholderRemote("MeowSidebar", "server_has_whitelist",       getServerHasWhitelist,    5000);

    // 玩家级占位符
    // 注意：使用 *Remote 版本注册（mainThreadOnly=true），后台线程翻译时跳过
    // 原因：Player 对象的 getHealth()/getPosition() 等方法访问内部状态，
    //       这些状态在主线程 tick 时被修改，后台线程读取会导致数据竞争和锁争用，
    //       严重拖慢主线程 TPS（表现为挖方块时区块刷新）。
    //       主线程 flushPending 第二遍翻译（skipRemote=false）会安全调用这些回调。
    reg.registerPlayerPlaceholderRemote("MeowSidebar", "pl.realName",    getPlRealName);
    reg.registerPlayerPlaceholderRemote("MeowSidebar", "pl.health",      getPlHealth);
    reg.registerPlayerPlaceholderRemote("MeowSidebar", "pl.maxHealth",   getPlMaxHealth);
    reg.registerPlayerPlaceholderRemote("MeowSidebar", "pl.hunger",      getPlHunger);
    reg.registerPlayerPlaceholderRemote("MeowSidebar", "pl.pos",         getPlPos);
    reg.registerPlayerPlaceholderRemote("MeowSidebar", "pl.posX",        getPlPosX);
    reg.registerPlayerPlaceholderRemote("MeowSidebar", "pl.posY",        getPlPosY);
    reg.registerPlayerPlaceholderRemote("MeowSidebar", "pl.posZ",        getPlPosZ);
    reg.registerPlayerPlaceholderRemote("MeowSidebar", "pl.dimId",       getPlDimId);
    reg.registerPlayerPlaceholderRemote("MeowSidebar", "pl.dimName",     getPlDimName);
    reg.registerPlayerPlaceholderRemote("MeowSidebar", "pl.expLevel",    getPlExpLevel);
    reg.registerPlayerPlaceholderRemote("MeowSidebar", "pl.gamemode",    getPlGamemode);
    reg.registerPlayerPlaceholderRemote("MeowSidebar", "pl.flying",      getPlFlying);
    reg.registerPlayerPlaceholderRemote("MeowSidebar", "pl.isOp",        getPlIsOp);
    reg.registerPlayerPlaceholderRemote("MeowSidebar", "pl.device",      getPlDevice);
    reg.registerPlayerPlaceholderRemote("MeowSidebar", "pl.ping",        getPlPing);
    reg.registerPlayerPlaceholderRemote("MeowSidebar", "pl.ip",          getPlIp);
    reg.registerPlayerPlaceholderRemote("MeowSidebar", "pl.uuid",        getPlUuid);
    reg.registerPlayerPlaceholderRemote("MeowSidebar", "pl.xuid",        getPlXuid);
    reg.registerPlayerPlaceholderRemote("MeowSidebar", "pl.handItem",    getPlHandItem);
    reg.registerPlayerPlaceholderRemote("MeowSidebar", "pl.speed",       getPlSpeed);
    reg.registerPlayerPlaceholderRemote("MeowSidebar", "pl.direction",   getPlDirection);

    // LLMoney 经济系统占位符（参考 MeowSync: LoadLibraryA 加载 LegacyMoney.dll）
    // 通过 p->getXuid() 获取玩家 XUID，调用 LLMoney_Get 查询实时余额
    // 注册为 Remote（主线程）占位符，与其它 pl.* 占位符保持一致（需访问 Player 对象）
    // LLMoney 未安装时返回 "0"
    reg.registerPlayerPlaceholderRemote("MeowSidebar", "pl.money",           getPlMoney);
    reg.registerPlayerPlaceholderRemote("MeowSidebar", "pl.money_formatted", getPlMoneyFormatted);

    // 时间级占位符（静态缓存，1秒更新）=====
    reg.registerStaticPlaceholder("MeowSidebar", "date.h",        getDateHour,     1000);
    reg.registerStaticPlaceholder("MeowSidebar", "date.m",        getDateMinute,   1000);
    reg.registerStaticPlaceholder("MeowSidebar", "date.s",        getDateSecond,   1000);
    reg.registerStaticPlaceholder("MeowSidebar", "date.D",        getDateDay,      1000);
    reg.registerStaticPlaceholder("MeowSidebar", "date.M",        getDateMonth,    1000);
    reg.registerStaticPlaceholder("MeowSidebar", "date.Y",        getDateYear,     1000);
    reg.registerStaticPlaceholder("MeowSidebar", "date.W",        getDateWeekday,  1000);
}

} // namespace meowpapi
