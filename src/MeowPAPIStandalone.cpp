// MeowPAPIStandalone.cpp - 独立 PAPI 前置插件主模块实现
//
// 在无 MeowSidebar / MeowMenu 时为 LSE 提供 PlaceholderAPI 服务
// LSE 插件可通过 ll.import("MeowPAPI", "translateString") 等接口使用占位符
#include "MeowPAPIStandalone.h"

#include "meowpapi/PlaceholderApi.h"
#include "meowpapi/Builtins.h"
#include "meowpapi/RemoteCallBridge.h"
#include "meowpapi/BepApiBridge.h"

#include "ll/api/mod/RegisterHelper.h"

namespace meowpapi_standalone {

MeowPAPIStandalone& MeowPAPIStandalone::getInstance() {
    static MeowPAPIStandalone instance;
    return instance;
}

bool MeowPAPIStandalone::load() {
    auto& logger = getSelf().getLogger();
    logger.info("MeowPAPI (Standalone) loading...");
    return true;
}

bool MeowPAPIStandalone::enable() {
    auto& logger = getSelf().getLogger();
    logger.info("MeowPAPI (Standalone) enabling...");

    // 检测是否已有 PAPI 服务端（如 MeowSidebar / MeowMenu）已加载
    // 避免重复初始化导致 RemoteCall 命名空间冲突
    if (meowpapi::isRemoteAvailable()) {
        logger.warn("检测到已存在 PAPI 服务端（可能 MeowSidebar / MeowMenu 已加载）。");
        logger.warn("MeowPAPI (Standalone) 跳过初始化，避免冲突。");
        logger.warn("如需使用独立前置，请卸载 MeowSidebar / MeowMenu 后重启服务器。");
        mInitialized = false;
        return true;
    }

    try {
        // 1. 初始化 PAPI 为服务端模式（直接使用本地 PlaceholderRegistry）
        meowpapi::PlaceholderApi::getInstance().initAsServer();

        // 2. 注册内置占位符（MSPT/TPS/服务器/玩家/时间/经济等）
        meowpapi::registerBuiltinPlaceholders();

        // 3. 导出 RemoteCall API（供 LSE / 其他 C++ 插件调用）
        meowpapi::exportRemoteCallApi();

        // 4. 安装 BEPAPI 兼容层（如果 BEPAPI 已加载，回退查询 BEPAPI 占位符）
        meowpapi::installBepApiFallback();

        mInitialized = true;
        logger.info("MeowPAPI (Standalone) 已启动。");
        logger.info("LSE 可通过 ll.import(\"MeowPAPI\", ...) 注册和使用占位符。");
        logger.info("可用接口：registerServerPlaceholder / registerPlayerPlaceholder /");
        logger.info("          registerStaticPlaceholder / unRegisterPlaceholder /");
        logger.info("          GetValue / GetValueWithPlayer / translateString /");
        logger.info("          translateStringWithPlayer / hasPlaceholder / listPlaceholders");
    } catch (std::exception const& e) {
        logger.error("MeowPAPI (Standalone) enable failed: {}", e.what());
    } catch (...) {
        logger.error("MeowPAPI (Standalone) enable failed: unknown exception");
    }
    return true;
}

bool MeowPAPIStandalone::disable() {
    auto& logger = getSelf().getLogger();
    logger.info("MeowPAPI (Standalone) disabling...");

    if (mInitialized) {
        // 移除 BEPAPI 兼容层
        meowpapi::removeBepApiFallback();
        // 移除 RemoteCall API 导出
        meowpapi::removeRemoteCallApi();
        mInitialized = false;
    }

    logger.info("MeowPAPI (Standalone) disabled.");
    return true;
}

} // namespace meowpapi_standalone

LL_REGISTER_MOD(meowpapi_standalone::MeowPAPIStandalone, meowpapi_standalone::MeowPAPIStandalone::getInstance());
