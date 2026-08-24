// PluginEntry.cpp - MeowPAPI 独立插件入口
//
// 将 MeowPAPI.dll 注册为 LeviLamina 原生插件，
// 由 LeviLamina 在启动时加载并自动初始化 PAPI 中心。
// 消费者插件（MeowSidebar / MeowMenu 等）不再需要嵌入 DLL，
// 只需链接静态库，静态库通过 GetModuleHandleW 检测已加载的 MeowPAPI.dll。
//
// lrca 为运行时可选依赖（导入表无 LegacyRemoteCall.dll）：
// 名称序 LegacyRemoteCall < MeowPAPI，正常部署下 lrca 先加载，enable 时
// attach 立即成功；若 lrca 缺失或后加载，exportRemoteCallApi 内部经
// ServerStartedEvent 兜底补导出，原生 C++ 占位符 API 始终可用。
#include "meowpapi/PlaceholderApi.h"
#include "meowpapi/PlaceholderRegistry.h"
#include "meowpapi/Builtins.h"
#include "meowpapi/RemoteCallBridge.h"
#include "meowpapi/BepApiBridge.h"
#include "meowpapi/DllExports.h"
#include "lse/LseBridge.h"

#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/server/ServerStartedEvent.h"

#include <ctime>
#include <fmt/format.h>

namespace meowpapi {

// 定义在 Commands.cpp（/meowpapi version|list 指令注册）
void registerCommands();

namespace {

// PE TimeDateStamp → "YYYY-MM-DD HH:MM" 可读时间（版本广播用）
std::string formatBuildTime(uint64_t ts) {
    if (ts == 0) return "unknown";
    time_t    t = static_cast<time_t>(ts);
    struct tm tmv;
    localtime_s(&tmv, &t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv);
    return buf;
}

} // anonymous namespace

class MeowPAPIPlugin {
public:
    static MeowPAPIPlugin& getInstance() {
        static MeowPAPIPlugin instance;
        return instance;
    }

    MeowPAPIPlugin() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    bool load() {
        getSelf().getLogger().info("MeowPAPI loading...");
        return true;
    }

    bool enable() {
        auto& logger = getSelf().getLogger();
        logger.info("MeowPAPI enabling...");

        // 1. 初始化为服务端模式
        PlaceholderApi::getInstance().initAsServer();

        // 2. 注册内置占位符（MSFT/TPS/服务器/玩家/时间等）
        registerBuiltinPlaceholders();

        // 3. 导出 RemoteCall API（lrca 已加载则立即导出，否则 ServerStarted 兜底）
        exportRemoteCallApi();

        // 4. 安装 BEPAPI 兼容层（如果 BEPAPI 已加载）
        installBepApiFallback();

        // 5. 注册 /meowpapi 指令（version 所有人 / list OP 自检）
        try {
            registerCommands();
        } catch (std::exception const& e) {
            logger.error("指令注册失败: {}", e.what());
        }

        // 6. ServerStarted 自动广播版本（开服日志可秒判部署版本，
        //    含 lrca 最终挂载状态与占位符总数）
        mVersionListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerStartedEvent>(
            [this](ll::event::ServerStartedEvent&) {
                auto& lg = getSelf().getLogger();
                auto  count = PlaceholderRegistry::getInstance().listPlaceholders().size();
                lg.info(
                    "MeowPAPI v{} (ABI 0x{:06x}, build {}) 已就绪: {} 个占位符, lrca{}",
                    MEOWPAPI_VERSION_STRING,
                    MEOWPAPI_ABI_VERSION,
                    formatBuildTime(MeowPAPI_GetBuildTimestamp()),
                    count,
                    lse::statusText()
                );
            }
        );

        logger.info(
            "MeowPAPI enabled. PAPI 中心已启动 (ABI 0x{:06x} = v{}, 带参PAPI, lrca: {})",
            MEOWPAPI_ABI_VERSION,
            MEOWPAPI_VERSION_STRING,
            lse::statusText()
        );
        return true;
    }

    bool disable() {
        auto& logger = getSelf().getLogger();
        logger.info("MeowPAPI disabling...");

        // 移除版本广播监听器
        if (mVersionListener) {
            ll::event::EventBus::getInstance().removeListener(mVersionListener);
            mVersionListener = nullptr;
        }

        // 移除 BEPAPI 兼容层和 RemoteCall API 导出
        removeBepApiFallback();
        removeRemoteCallApi();

        logger.info("MeowPAPI disabled.");
        return true;
    }

private:
    ll::mod::NativeMod&     mSelf;
    ll::event::ListenerPtr  mVersionListener = nullptr;
};

} // namespace meowpapi

LL_REGISTER_MOD(meowpapi::MeowPAPIPlugin, meowpapi::MeowPAPIPlugin::getInstance());
