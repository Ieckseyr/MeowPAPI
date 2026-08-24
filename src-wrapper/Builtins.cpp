// Builtins.cpp - 内置占位符注册（静态库包装层）
//
// 转发到 MeowPAPI.dll 的 MeowPAPI_RegisterBuiltinPlaceholders
#include "meowpapi/Builtins.h"
#include "DllLoader.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>

namespace meowpapi {

namespace {

void debugLog(char const* fmt, ...) {
    try {
        char buf[2048];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        auto now  = std::chrono::system_clock::now();
        auto t    = std::chrono::system_clock::to_time_t(now);
        auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        struct tm tmv;
        localtime_s(&tmv, &t);

        char line[2300];
        int prefixLen = snprintf(line, sizeof(line),
            "[%04d-%02d-%02d %02d:%02d:%02d.%03d][Builtins] ",
            tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
            tmv.tm_hour, tmv.tm_min, tmv.tm_sec, (int)ms.count());
        snprintf(line + prefixLen, sizeof(line) - prefixLen, "%s\n", buf);

        static std::mutex logMtx;
        std::lock_guard<std::mutex> lock(logMtx);
        std::ofstream f("meowpapi_loader_debug.log", std::ios::app);
        if (f.is_open()) {
            f.write(line, strlen(line));
        }
    } catch (...) {}
}

} // anonymous namespace

void registerBuiltinPlaceholders() {
    debugLog("registerBuiltinPlaceholders: called");
    auto& loader = DllLoader::getInstance();
    if (!loader.load()) {
        debugLog("registerBuiltinPlaceholders: loader.load() FAILED");
        return;
    }
    // 自动初始化模式下已由 DllLoader 调用，跳过
    if (loader.isAutoInitialized()) {
        debugLog("registerBuiltinPlaceholders: skipped (autoInitialized)");
        return;
    }
    auto* f = loader.functions();
    if (!f) {
        debugLog("registerBuiltinPlaceholders: loader.functions() returned nullptr");
        return;
    }
    debugLog("registerBuiltinPlaceholders: calling DLL f->registerBuiltinPlaceholders()");
    f->registerBuiltinPlaceholders();
    debugLog("registerBuiltinPlaceholders: done");
}

} // namespace meowpapi
