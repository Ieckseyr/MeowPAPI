// Builtins.h - 内置原生占位符注册
//
// 移植自 CoralFans (MSPT/TPS) 和 BetterSidebar (服务器/玩家/时间变量)
// 所有内置占位符注册到 PlaceholderRegistry 单例
#pragma once

#include <string>

namespace meowpapi {

// 注册所有内置原生占位符
// 应在 MeowSidebar enable 阶段调用
void registerBuiltinPlaceholders();

} // namespace meowpapi
