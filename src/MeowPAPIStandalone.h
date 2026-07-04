// MeowPAPIStandalone.h - 独立 PAPI 前置插件主模块
//
// 在无 MeowSidebar / MeowMenu 时为 LSE 提供 PlaceholderAPI 服务
// 启动时初始化 MeowPAPI 为服务端模式，注册内置占位符，导出 RemoteCall API
#pragma once

#include "ll/api/mod/NativeMod.h"

namespace meowpapi_standalone {

class MeowPAPIStandalone {
public:
    static MeowPAPIStandalone& getInstance();

    MeowPAPIStandalone() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    bool load();
    bool enable();
    bool disable();

private:
    ll::mod::NativeMod& mSelf;
    bool                mInitialized = false;
};

} // namespace meowpapi_standalone
