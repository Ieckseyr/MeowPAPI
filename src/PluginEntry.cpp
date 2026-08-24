// PluginEntry.cpp - MeowPAPI 独立插件入口
//
// 将 MeowPAPI.dll 注册为 LeviLamina 原生插件，
// 由 LeviLamina 在启动时加载并自动初始化 PAPI 中心。
// 消费者插件（MeowSidebar / MeowMenu 等）不再需要嵌入 DLL，
// 只需链接静态库，静态库通过 GetModuleHandleW 检测已加载的 MeowPAPI.dll。
//
// MeowPAPI.dll 通过 __declspec(dllimport) 硬依赖 LegacyRemoteCall.dll，
// Windows 加载器保证 lrca 先于 MeowPAPI.dll 加载，初始化时序正确。
#include "meowpapi/PlaceholderApi.h"
#include "meowpapi/Builtins.h"
#include "meowpapi/RemoteCallBridge.h"
#include "meowpapi/BepApiBridge.h"
#include "meowpapi/DllExports.h"

#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"

namespace meowpapi {

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

        // 2. 注册内置占位符（MSPT/TPS/服务器/玩家/时间等）
        registerBuiltinPlaceholders();

        // 3. 导出 RemoteCall API
        //    硬依赖方案下 lrca 在 MeowPAPI.dll 加载前已就绪，直接导出
        exportRemoteCallApi();

        // 4. 安装 BEPAPI 兼容层（如果 BEPAPI 已加载）
        installBepApiFallback();

        logger.info(
            "MeowPAPI enabled. PAPI 中心已启动，支持 RemoteCall 注册。"
            "(ABI 0x{:06x} = v{}, 带参PAPI)",
            MEOWPAPI_ABI_VERSION,
            MEOWPAPI_VERSION_STRING
        );
        return true;
    }

    bool disable() {
        auto& logger = getSelf().getLogger();
        logger.info("MeowPAPI disabling...");

        // 移除 BEPAPI 兼容层和 RemoteCall API 导出
        removeBepApiFallback();
        removeRemoteCallApi();

        logger.info("MeowPAPI disabled.");
        return true;
    }

private:
    ll::mod::NativeMod& mSelf;
};

} // namespace meowpapi

LL_REGISTER_MOD(meowpapi::MeowPAPIPlugin, meowpapi::MeowPAPIPlugin::getInstance());
