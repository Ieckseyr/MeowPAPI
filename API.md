```js
// 注册占位符
ll.import("MeowPAPI", "registerServerPlaceholder")(pluginName, papiName, callbackNs, callbackFn)
ll.import("MeowPAPI", "registerPlayerPlaceholder")(pluginName, papiName, callbackNs, callbackFn)
ll.import("MeowPAPI", "registerStaticPlaceholder")(pluginName, papiName, callbackNs, callbackFn, intervalMs)

// 翻译/查询
ll.import("MeowPAPI", "translateString")(str)
ll.import("MeowPAPI", "translateStringWithPlayer")(str, playerName)
ll.import("MeowPAPI", "GetValue")(papiName)
ll.import("MeowPAPI", "GetValueWithPlayer")(papiName, playerName)

// 管理
ll.import("MeowPAPI", "unRegisterPlaceholder")(papiName)
ll.import("MeowPAPI", "hasPlaceholder")(papiName)
ll.import("MeowPAPI", "listPlaceholders")()
```