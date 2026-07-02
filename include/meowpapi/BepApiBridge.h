// BepApiBridge.h - BEPlaceholderAPI 双向兼容层
//
// 功能：
// 1. 检测 BEPAPI 是否已加载
// 2. 安装回退解析器：当 MeowSidebar 本地找不到占位符时，回退到 BEPAPI 查询
// 3. 这样 LSE 插件通过 BEPAPI 注册的占位符，MeowSidebar 也能访问
//
// 注意：MeowSidebar 的内置占位符不需要同步到 BEPAPI，
// 因为 LSE 插件可以直接通过 ll.import("MeowSidebar", "translateString") 访问
#pragma once

namespace meowpapi {

// 检测 BEPAPI 是否已加载
bool isBepApiAvailable();

// 安装 BEPAPI 回退解析器到 PlaceholderRegistry
// MeowSidebar 在 enable 阶段调用（如果检测到 BEPAPI）
void installBepApiFallback();

// 移除 BEPAPI 回退解析器
// MeowSidebar 在 disable 阶段调用
void removeBepApiFallback();

} // namespace meowpapi
