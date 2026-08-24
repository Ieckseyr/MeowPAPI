// EnsureLoaded.cpp - meowpapi::ensureLoaded() 实现(静态库包装层)
//
// 转发到 DllLoader::getInstance().load(),后者负责:
//   1. 检测 MeowPAPI.dll 是否已加载
//   2. 从嵌入数据释放并加载
//   3. 自动初始化(initAsServer + registerBuiltinPlaceholders + exportRemoteCallApi +
//      installBepApiFallback)
//
// 消费者插件只需调用此单一函数,无需手动调用上述 4 个初始化函数。
#include "meowpapi/EnsureLoaded.h"
#include "DllLoader.h"

namespace meowpapi {

bool ensureLoaded() {
    return DllLoader::getInstance().load(/*skipAutoInit=*/false);
}

bool isDeployer() {
    return DllLoader::getInstance().isDeployed();
}

uint32_t loadedAbiVersion() {
    return DllLoader::getInstance().loadedAbiVersion();
}

uint32_t loadedAbiFeatures() {
    return DllLoader::getInstance().loadedAbiFeatures();
}

bool paramPapiSupported() {
    return DllLoader::getInstance().paramPapiSupported();
}

uint64_t loadedBuildTimestamp() {
    return DllLoader::getInstance().loadedBuildTimestamp();
}

} // namespace meowpapi
