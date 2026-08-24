// BepApiBridge.cpp - BEPlaceholderAPI 兼容层（静态库包装层）
//
// 转发到 MeowPAPI.dll 的导出函数。
#include "meowpapi/BepApiBridge.h"
#include "DllLoader.h"

namespace meowpapi {

bool isBepApiAvailable() {
    auto& loader = DllLoader::getInstance();
    if (!loader.load()) return false;
    auto* f = loader.functions();
    return f ? (f->isBepApiAvailable() != 0) : false;
}

void installBepApiFallback() {
    auto& loader = DllLoader::getInstance();
    if (!loader.load()) return;
    // 自动初始化模式下已由 DllLoader 调用，跳过
    if (loader.isAutoInitialized()) return;
    auto* f = loader.functions();
    if (!f) return;
    f->installBepApiFallback();
}

void removeBepApiFallback() {
    auto& loader = DllLoader::getInstance();
    if (!loader.load()) return;
    auto* f = loader.functions();
    if (!f) return;
    f->removeBepApiFallback();
}

} // namespace meowpapi
