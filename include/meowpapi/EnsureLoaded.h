// EnsureLoaded.h - MeowPAPI 消费者插件标准化加载入口
//
// 所有使用 MeowPAPI 的消费者插件应在自身的 load() 阶段调用 meowpapi::ensureLoaded()。
// 该函数会:
//   1. 检测 MeowPAPI.dll 是否已由 LeviLamina 作为独立插件加载
//   2. 若未加载,从消费者插件嵌入的二进制数据中释放 MeowPAPI.dll 和 manifest.json
//      到 plugins/MeowPAPI/ 目录,然后 LoadLibraryW 加载
//   3. 执行自动初始化:initAsServer + registerBuiltinPlaceholders + exportRemoteCallApi +
//      installBepApiFallback
//   4. 多消费者插件防冲突:第一个加载的消费者执行部署和初始化,后续消费者直接连接
//
// 用法:
//   bool MyPlugin::load() {
//       if (!meowpapi::ensureLoaded()) {
//           getSelf().getLogger().error("MeowPAPI 加载失败");
//           return false;
//       }
//       return true;
//   }
//
// 注意:消费者插件无需(也不应)手动调用 initAsServer()、registerBuiltinPlaceholders()、
//       exportRemoteCallApi()、installBepApiFallback() — 这些已由 ensureLoaded() 自动完成。
//       仅当消费者本身就是 MeowPAPI 的宿主(如 MeowSidebar)时,才需调用 initAsServer() 等。
#pragma once

namespace meowpapi {

// 确保 MeowPAPI.dll 已加载并完成自动初始化。
// 返回 true 表示已就绪(本次调用或之前的调用已完成初始化)。
// 返回 false 表示加载失败(DLL 缺失、架构不匹配等)。
// 线程安全,可多次调用。
bool ensureLoaded();

// 当前消费者插件是否为 MeowPAPI 的部署者(第一个调用 ensureLoaded 并触发部署的插件)。
// 仅部署者应在 disable() 阶段调用 meowpapi::removeRemoteCallApi() 等全局清理。
// 非部署者只需清理自己的 RemoteCall 命名空间。
// 线程安全。
bool isDeployer();

} // namespace meowpapi
