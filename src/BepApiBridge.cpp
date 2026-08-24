// BepApiBridge.cpp - BEPlaceholderAPI 双向兼容层实现
#include "meowpapi/BepApiBridge.h"
#include "meowpapi/PlaceholderRegistry.h"
#include "meowpapi/RemoteCallAPI.h"

#include "mc/world/actor/player/Player.h"

namespace meowpapi {

bool isBepApiAvailable() {
    // 经 LseBridge 转发到 lrca（未挂载时返回 false，BEPAPI 兼容层不安装）
    return RemoteCall::hasFunc("BEPlaceholderAPI", "GetValue");
}

void installBepApiFallback() {
    if (!isBepApiAvailable()) return;

    PlaceholderRegistry::getInstance().setFallbackResolver(
        [](std::string const& name, Player* player) -> std::string {
            if (!isBepApiAvailable()) return "{" + name + "}";
            try {
                if (player) {
                    // 玩家级占位符：通过 BEPAPI 的 GetValueWithPlayer 查询
                    auto fn = RemoteCall::importAs<std::string(std::string const&, std::string const&)>(
                        "BEPlaceholderAPI", "GetValueWithPlayer"
                    );
                    auto result = fn(name, player->getRealName());
                    // BEPAPI 找不到时返回原始格式 %name%，转换为 {name}
                    if (result == "%" + name + "%") return "{" + name + "}";
                    return result;
                } else {
                    // 服务器级占位符：通过 BEPAPI 的 GetValue 查询
                    auto fn = RemoteCall::importAs<std::string(std::string const&)>(
                        "BEPlaceholderAPI", "GetValue"
                    );
                    auto result = fn(name);
                    if (result == "%" + name + "%") return "{" + name + "}";
                    return result;
                }
            } catch (...) {
                return "{" + name + "}";
            }
        }
    );
}

void removeBepApiFallback() {
    PlaceholderRegistry::getInstance().setFallbackResolver(nullptr);
}

} // namespace meowpapi
