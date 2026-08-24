// LseBridge.h - LegacyRemoteCall 运行时可选挂载桥
//
// MeowPAPI.dll 不以 lrca 为编译期/导入表依赖，运行时通过
// GetModuleHandleW + 符号解析挂载。符号解析两级策略：
//   1. 精确匹配：完整 mangled 符号 GetProcAddress（与本地 lrca 0.18.0
//      导出表逐字一致）
//   2. 模糊匹配：精确失败时解析 PE 导出表，按 "?exportFunc@RemoteCall@@YA"
//      等前缀匹配——跨 lrca 编译版本（MSVC STL std::function mangling
//      差异）兼容。MSVC ABI 自 2015 起 std::function/string 布局稳定，
//      前缀匹配到的函数二进制兼容。
//
// 候选模块（任一命中即挂载）：
//   - LegacyRemoteCall.dll          独立 lrca（LeviLamina 插件）
//   - legacy-script-engine-*.dll    LSE 引擎内置 RemoteCall（若未来内置）
//
// 行为契约：
// - 候选模块均未加载 → false（后续调用可再试，不置失败标志）
// - 模块在但符号两级解析均失败 → 置失败标志（版本彻底不兼容），
//   诊断快照记录明细，可经 status() 供 /meowpapi version 展示
// - RemoteCall::* 调用未挂载时安全降级（返回 false/空）
// - 原生 C++ 占位符 API 不经过本桥，不受影响
#pragma once

#include <string>

namespace meowpapi::lse {

// 尝试挂载 lrca（幂等，重复调用安全）
// 返回：true = 已挂载（本次或之前）；false = 不可用
bool attach();

// 当前是否已挂载
bool isAttached();

// 挂载状态诊断（/meowpapi version 与启动广播用）
struct Status {
    bool        attached   = false; // 已成功挂载
    bool        tried      = false; // 至少尝试过一次解析
    bool        failed     = false; // 找到模块但符号解析彻底失败
    bool        fuzzy      = false; // 经导出表前缀模糊匹配挂载
    std::string moduleName;         // 命中/尝试的模块名
};

// 返回当前挂载状态快照（线程安全）
Status status();

// 人类可读的一行状态描述（"已挂载(LegacyRemoteCall.dll, 模糊匹配)" /
// "模块已加载但符号不匹配(LegacyRemoteCall.dll)" / "未找到 lrca 模块"）
std::string statusText();

} // namespace meowpapi::lse
