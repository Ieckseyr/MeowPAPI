// RemoteCallAPI.h - MeowPAPI 双模式版本
//
// 这是 LegacyRemoteCall 的 RemoteCallAPI.h 的修改版本，支持两种模式：
//
// 1. DLL 模式（编译 MeowPAPI.dll 时，定义 MEOWPAPI_DLL_EXPORTS）：
//    通过 __declspec(dllimport) 直接从 LegacyRemoteCall.dll 导入函数。
//    MeowPAPI.dll 硬依赖 lrca，导入表包含 LegacyRemoteCall.dll，
//    Windows 加载器保证 lrca 先于 MeowPAPI.dll 加载，初始化时序正确。
//
// 2. 代理模式（编译消费者插件时，不定义 MEOWPAPI_DLL_EXPORTS）：
//    通过函数指针调用 MeowPAPI.dll 中导出的 RC_* 代理函数，
//    使消费者插件无需链接 legacyremotecall。函数指针由 DllLoader::load()
//    通过 GetProcAddress 初始化。
//
// 用法：用 #include "meowpapi/RemoteCallAPI.h" 替代 #include <RemoteCallAPI.h>
// API 完全兼容：RemoteCall::exportAs / RemoteCall::importAs 等用法不变。
#pragma once
#include "fmt/format.h"
#include "ll/api/base/TypeTraits.h"
#include "ll/api/utils/SystemUtils.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/Container.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"

///////////////////////////////////////////////////////
// Remote Call API (MeowPAPI 双模式版本)
// DLL 模式：__declspec(dllimport) 直接从 lrca 导入（MeowPAPI.dll 用）
// 代理模式：通过 MeowPAPI.dll 的 RC_* 代理转发（消费者插件用）
// 用法与原版完全相同：
//   RemoteCall::exportAs("TestNameSpace", "strSize", [](std::string const& arg) -> int { return arg.size(); });
//   auto strSize = RemoteCall::importAs<int(std::string const&)>("TestNameSpace", "strSize");
/////////////////////////////////////////////////////

// 双模式宏：DLL 模式下 REMOTE_CALL_API = __declspec(dllimport)，
// 代理模式下为空（使用 inline + p_* 函数指针）
#ifdef MEOWPAPI_DLL_EXPORTS
#define REMOTE_CALL_API __declspec(dllimport)
#else
#define REMOTE_CALL_API
#endif

namespace RemoteCall {

struct NbtType {
    CompoundTag const* ptr = nullptr;
    bool               own = false;
    NbtType(std::unique_ptr<CompoundTag> tag) noexcept /*NOLINT*/ : ptr(tag.release()), own(true) {}
    NbtType(CompoundTag const* ptr) noexcept /*NOLINT*/ : ptr(ptr), own(false) {}
    NbtType(NbtType const& other) {
        if (other.own) {
            ptr = new CompoundTag(*other.ptr);
            own = true;
        } else {
            ptr = other.ptr;
            own = false;
        }
    }
    NbtType(NbtType&& other) noexcept {
        std::swap(ptr, other.ptr);
        std::swap(own, other.own);
    }
    NbtType& operator=(NbtType const& other) {
        if (this != &other) {
            if (own) delete const_cast<CompoundTag*>(ptr);
            if (other.own) {
                ptr = new CompoundTag(*other.ptr);
                own = true;
            } else {
                ptr = other.ptr;
                own = false;
            }
        }
        return *this;
    }
    NbtType& operator=(NbtType&& other) noexcept {
        std::swap(ptr, other.ptr);
        std::swap(own, other.own);
        return *this;
    }
    ~NbtType() {
        if (own) {
            delete const_cast<CompoundTag*>(ptr);
        }
    }
    inline std::unique_ptr<CompoundTag> tryGetUniquePtr() {
        if (!own) return {};
        own       = false;
        auto uptr = std::unique_ptr<CompoundTag>(const_cast<CompoundTag*>(ptr));
        ptr       = nullptr;
        return std::move(uptr);
    }
    template <typename RTN>
    inline RTN get() = delete;
    template <>
    inline CompoundTag const* get() {
        return ptr;
    };
    template <>
    inline CompoundTag* get() {
        return const_cast<CompoundTag*>(ptr);
    };
    template <>
    inline std::unique_ptr<CompoundTag> get() {
        return tryGetUniquePtr();
    };
};

struct ItemType {
    ItemStack const* ptr = nullptr;
    bool             own = false;
    ItemType(std::unique_ptr<ItemStack> tag) noexcept /*NOLINT*/ : ptr(tag.release()), own(true) {}
    ItemType(ItemStack const* ptr) noexcept /*NOLINT*/ : ptr(ptr), own(false) {}
    ItemType(ItemType const& other) {
        if (other.own) {
            ptr = new ItemStack(*other.ptr);
            own = true;
        } else {
            ptr = other.ptr;
            own = false;
        }
    }
    ItemType(ItemType&& other) noexcept {
        std::swap(ptr, other.ptr);
        std::swap(own, other.own);
    }
    ItemType& operator=(ItemType const& other) {
        if (this != &other) {
            if (own) delete const_cast<ItemStack*>(ptr);
            if (other.own) {
                ptr = new ItemStack(*other.ptr);
                own = true;
            } else {
                ptr = other.ptr;
                own = false;
            }
        }
        return *this;
    }
    ItemType& operator=(ItemType&& other) noexcept {
        std::swap(ptr, other.ptr);
        std::swap(own, other.own);
        return *this;
    }
    ~ItemType() {
        if (own) {
            delete const_cast<ItemStack*>(ptr);
        }
    }
    inline std::unique_ptr<ItemStack> tryGetUniquePtr() {
        if (!own) return {};
        own       = false;
        auto uptr = std::unique_ptr<ItemStack>(const_cast<ItemStack*>(ptr));
        ptr       = nullptr;
        return std::move(uptr);
    }
    template <typename RTN>
    inline RTN get() = delete;
    template <>
    inline ItemStack const* get() {
        return ptr;
    };
    template <>
    inline ItemStack* get() {
        return const_cast<ItemStack*>(ptr);
    };
    template <>
    inline std::unique_ptr<ItemStack> get() {
        return tryGetUniquePtr();
    };
};

struct BlockType {
    Block const* block;
    BlockPos     blockPos;
    int          dimension;
    BlockType(Block* block) noexcept /*NOLINT*/ : block(block) {};
    BlockType(Block const* ptr) noexcept /*NOLINT*/ : block(ptr) {
        blockPos  = BlockPos::ZERO();
        dimension = 0;
    };
    template <typename RTN>
    inline RTN get() = delete;
    template <>
    inline Block const* get() {
        return block;
    };
};

struct NumberType {
    __int64 i = 0;
    double  f = 0;
    NumberType(__int64 i, double f) : i(i), f(f) {};
    template <typename T, std::enable_if_t<std::is_integral_v<T> || std::is_floating_point_v<T>>>
    NumberType& operator=(T&& v) noexcept {
        i = static_cast<__int64>(v);
        f = static_cast<double>(v);
        return *this;
    }
    NumberType(double v) noexcept /*NOLINT*/ : i(static_cast<__int64>(v)), f(static_cast<double>(v)) {};
    NumberType(float v) noexcept /*NOLINT*/ : i(static_cast<__int64>(v)), f(static_cast<double>(v)) {};
    NumberType(__int64 v) noexcept /*NOLINT*/ : i(static_cast<__int64>(v)), f(static_cast<double>(v)) {};
    NumberType(int v) noexcept /*NOLINT*/ : i(static_cast<__int64>(v)), f(static_cast<double>(v)) {};
    NumberType(short v) noexcept /*NOLINT*/ : i(static_cast<__int64>(v)), f(static_cast<double>(v)) {};
    NumberType(char v) noexcept /*NOLINT*/ : i(static_cast<__int64>(v)), f(static_cast<double>(v)) {};
    NumberType(unsigned __int64 v) noexcept /*NOLINT*/ : i(static_cast<__int64>(v)), f(static_cast<double>(v)) {};
    NumberType(unsigned int v) noexcept /*NOLINT*/ : i(static_cast<__int64>(v)), f(static_cast<double>(v)) {};
    NumberType(unsigned short v) noexcept /*NOLINT*/ : i(static_cast<__int64>(v)), f(static_cast<double>(v)) {};
    NumberType(unsigned char v) noexcept /*NOLINT*/ : i(static_cast<__int64>(v)), f(static_cast<double>(v)) {};
    // 整数从 i 读, 浮点必须从 f 读 —— 原实现统一从 i 转换导致 RemoteCall 传参丢失小数
    // (悬浮字坐标 x+0.5 被 static_cast<__int64> 截断, 显示偏到方块角)
    template <typename RTN>
    inline std::enable_if_t<std::is_integral_v<RTN>, RTN> get() {
        return static_cast<RTN>(i);
    };
    template <typename RTN>
    inline std::enable_if_t<std::is_floating_point_v<RTN>, RTN> get() {
        return static_cast<RTN>(f);
    };
};

struct WorldPosType {
    Vec3 pos   = Vec3::ZERO();
    int  dimId = 3; // VanillaDimensions::Undefined;
    WorldPosType(Vec3 const& pos, int dimId = 3) noexcept /*NOLINT*/ : pos(pos), dimId(dimId) {};
    WorldPosType(std::pair<Vec3, int> const& pos) noexcept /*NOLINT*/ : pos(pos.first), dimId(pos.second) {};
    template <typename RTN>
    inline RTN get() = delete;
    template <>
    inline Vec3 get() {
        return pos;
    };
    template <>
    inline BlockPos get() {
        return BlockPos(pos);
    };
    template <>
    inline std::pair<Vec3, int> get() {
        return std::make_pair(pos, dimId);
    };
    template <>
    inline std::pair<BlockPos, int> get() {
        return std::make_pair(BlockPos(pos), dimId);
    };
};

struct BlockPosType {
    BlockPos pos   = BlockPos::ZERO();
    int      dimId = 0;
    BlockPosType(BlockPos const& pos, int dimId = 0) noexcept /*NOLINT*/ : pos(pos), dimId(dimId) {};
    BlockPosType(std::pair<BlockPos, int> const& pos) noexcept /*NOLINT*/ : pos(pos.first), dimId(pos.second) {};
    template <typename RTN>
    inline RTN get() = delete;
    template <>
    inline BlockPos get() {
        return pos;
    };
    template <>
    inline std::pair<BlockPos, int> get() {
        return std::make_pair(pos, dimId);
    };
    template <>
    inline Vec3 get() {
        return pos;
    };
    template <>
    inline std::pair<Vec3, int> get() {
        return std::make_pair(pos, dimId);
    };
};

// std::string  -> json
// std::string* -> bytes
template <typename Ty>
static constexpr bool is_extra_type_v = ll::traits::is_one_of_v<
    Ty,
    std::nullptr_t,
    NumberType,
    Player*,
    Actor*,
    BlockActor*,
    Container*,
    WorldPosType,
    BlockPosType,
    ItemType,
    BlockType,
    NbtType>;


static_assert(
    sizeof(std::variant<
           bool,
           std ::string,
           std ::nullptr_t,
           NumberType,
           Player*,
           Actor*,
           BlockActor*,
           Container*,
           WorldPosType,
           BlockPosType,
           ItemType,
           BlockType,
           NbtType>)
    == 40
);

template <typename>
constexpr bool is_vector_v = false;
template <class Ty, class Alloc>
constexpr bool is_vector_v<std::vector<Ty, Alloc>> = true;
template <typename>
constexpr bool is_map_v = false;
template <class Kty, class Ty, class Pr, class Alloc>
constexpr bool is_map_v<std::map<Kty, Ty, Pr, Alloc>> = true;
template <class Kty, class Ty, class Hasher, class Keyeq, class Alloc>
constexpr bool is_map_v<std::unordered_map<Kty, Ty, Hasher, Keyeq, Alloc>> = true;
using Value                                                                = std::variant<
                                                                   bool,
                                                                   std ::string,
                                                                   std ::nullptr_t,
                                                                   NumberType,
                                                                   Player*,
                                                                   Actor*,
                                                                   BlockActor*,
                                                                   Container*,
                                                                   WorldPosType,
                                                                   BlockPosType,
                                                                   ItemType,
                                                                   BlockType,
                                                                   NbtType>;

struct ValueType {
    using ArrayType  = std::vector<ValueType>;
    using ObjectType = std::unordered_map<std::string, ValueType>;
    using Type       = std::variant<Value, ArrayType, ObjectType>;
    Type value;
    ValueType()                 = default;
    ValueType(ValueType const&) = default;
    ValueType(ValueType&&)      = default;
    template <
        typename T,
        typename = std::enable_if_t<
            !std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, ValueType>
            && std::is_constructible_v<Type, T>>>
    ValueType(T&& v) /*NOLINT*/ : value(std::forward<T>(v)) {}
};

template <typename Ty>
static constexpr bool is_supported_type_v =
    std::is_void_v<Ty>
    || ll::traits::is_one_of_v<
        Ty,
        bool,
        std ::string,
        std ::nullptr_t,
        NumberType,
        Player*,
        Actor*,
        BlockActor*,
        Container*,
        WorldPosType,
        BlockPosType,
        ItemType,
        BlockType,
        NbtType>
    || std::is_assignable_v<NumberType, Ty> || std::is_assignable_v<NbtType, Ty> || std::is_assignable_v<BlockType, Ty>
    || std::is_assignable_v<ItemType, Ty> || std::is_assignable_v<WorldPosType, Ty>
    || std::is_assignable_v<BlockPosType, Ty> || std::is_base_of_v<Player, std::remove_pointer_t<Ty>>
    || std::is_base_of_v<Actor, std::remove_pointer_t<Ty>>;

template <typename RTN>
RTN extract(ValueType&& val);
template <typename T>
ValueType pack(T val);

template <typename RTN>
RTN extractValue(Value&& value) {
    using Type = std::remove_const_t<std::remove_reference_t<RTN>>;
    static_assert(is_supported_type_v<Type>, "Unsupported Type:");
    if constexpr (ll::traits::is_one_of_v<
                      Type,
                      bool,
                      std ::string,
                      std ::nullptr_t,
                      NumberType,
                      Player*,
                      Actor*,
                      BlockActor*,
                      Container*,
                      WorldPosType,
                      BlockPosType,
                      ItemType,
                      BlockType,
                      NbtType>)
        return std::get<Type>(value);
    else if constexpr (std::is_assignable_v<NumberType, RTN>) return std::get<NumberType>(value).get<Type>();
    else if constexpr (std::is_assignable_v<NbtType, RTN>) return std::get<NbtType>(value).get<Type>();
    else if constexpr (std::is_assignable_v<ItemType, RTN>) return std::get<ItemType>(value).get<Type>();
    else if constexpr (std::is_assignable_v<BlockType, RTN>) return std::get<BlockType>(value).get<Type>();
    else if constexpr (std::is_assignable_v<WorldPosType, RTN>) return std::get<WorldPosType>(value).get<Type>();
    else if constexpr (std::is_assignable_v<BlockPosType, RTN>) return std::get<BlockPosType>(value).get<Type>();
    else if constexpr (std::is_base_of_v<Player, std::remove_pointer_t<RTN>>)
        return static_cast<RTN>(std::get<Player*>(value));
    else if constexpr (std::is_base_of_v<Actor, std::remove_pointer_t<RTN>>)
        return static_cast<RTN>(std::get<Actor*>(value));
    else if constexpr (std::is_void_v<Type>) return;
    else throw std::exception(fmt::format(__FUNCTION__ " - Unsupported Type: {}", typeid(RTN).name()).c_str());
}

template <typename RTN>
bool extractValue(std::vector<ValueType>& value, std::vector<RTN>& rtn) {
    for (ValueType& val : value) {
        rtn.emplace_back(std::move(extract<RTN>(std::move(val))));
    }
    return true;
}

template <typename RTN>
bool extractValue(std::unordered_map<std::string, ValueType>& value, std::unordered_map<std::string, RTN>& rtn) {
    for (auto& [key, val] : value) {
        rtn.emplace(key, std::move(extract<RTN>(std::move(val))));
    }
    return true;
}

template <typename RTN>
RTN extract(ValueType&& val) {
    if constexpr (is_vector_v<RTN>) {
        RTN rtn{};
        extractValue(std::get<std::vector<ValueType>>(val.value), rtn);
        return std::move(rtn);
    } else if constexpr (is_map_v<RTN>) {
        RTN rtn{};
        extractValue(std::get<std::unordered_map<std::string, ValueType>>(val.value), rtn);
        return std::move(rtn);
    } else return extractValue<RTN>(std::move(std::get<Value>(val.value)));
}

template <typename T>
ValueType packValue(T val) {
    using RawType = std::remove_reference_t<std::remove_const_t<T>>;
    static_assert(is_supported_type_v<RawType>, "Unsupported Type");
    if constexpr (ll::traits::is_one_of_v<
                      RawType,
                      bool,
                      std ::string,
                      std ::nullptr_t,
                      NumberType,
                      Player*,
                      Actor*,
                      BlockActor*,
                      Container*,
                      WorldPosType,
                      BlockPosType,
                      ItemType,
                      BlockType,
                      NbtType>)
        return ValueType(std::forward<T>(val));
    else if constexpr (std::is_assignable_v<NumberType, T>) return ValueType(NumberType{std::forward<T>(val)});
    else if constexpr (std::is_assignable_v<NbtType, T>) return ValueType(NbtType(std::forward<T>(val)));
    else if constexpr (std::is_assignable_v<ItemType, T>) return ValueType(ItemType(std::forward<T>(val)));
    else if constexpr (std::is_assignable_v<BlockType, T>) return ValueType(BlockType(std::forward<T>(val)));
    else if constexpr (std::is_assignable_v<WorldPosType, T>) return ValueType(WorldPosType(std::forward<T>(val)));
    else if constexpr (std::is_assignable_v<BlockPosType, T>) return ValueType(BlockPosType(std::forward<T>(val)));
    else if constexpr (std::is_base_of_v<Player, std::remove_pointer_t<T>>)
        return ValueType(static_cast<Player*>(std::forward<T>(val)));
    else if constexpr (std::is_base_of_v<Actor, std::remove_pointer_t<T>>)
        return ValueType(static_cast<Actor*>(std::forward<T>(val)));
    else if constexpr (std::is_void_v<RawType>) return {};
    throw std::runtime_error(fmt::format(__FUNCTION__ " - Unsupported Type: {}", typeid(T).name()).c_str());
}
template <typename T>
std::vector<ValueType> packArray(std::vector<T> const& val) {
    std::vector<ValueType> result;
    for (auto& v : val) {
        result.emplace_back(pack(v));
    }
    return result;
}
template <typename T>
std::unordered_map<std::string, ValueType> packObject(std::unordered_map<std::string, T> const& val) {
    std::unordered_map<std::string, ValueType> result;
    for (auto& [k, v] : val) {
        result.emplace(k, pack(v));
    }
    return result;
}

template <typename T>
ValueType pack(T val) {
    using RawType = std::remove_reference_t<std::remove_const_t<T>>;
    if constexpr (is_vector_v<RawType>) {
        return packArray(std::forward<T>(val));
    } else if constexpr (is_map_v<RawType>) {
        return packObject(std::forward<T>(val));
    } else return packValue(std::forward<T>(val));
}


using CallbackFn = std::function<ValueType(std::vector<ValueType>)>;

#ifdef MEOWPAPI_DLL_EXPORTS
// ===== DLL 模式：MeowPAPI.dll 内部直接从 LegacyRemoteCall.dll 导入 =====
// __declspec(dllimport) 让编译器生成间接调用（通过导入表），链接时需要
// LegacyRemoteCall.lib。MeowPAPI.dll 导入表硬依赖 lrca，Windows 加载器
// 保证 lrca 先加载，ll_mod_load 初始化时序正确，无需运行时动态解析。
REMOTE_CALL_API extern CallbackFn const EMPTY_FUNC;
REMOTE_CALL_API bool                    exportFunc(
                       std::string const& nameSpace,
                       std::string const& funcName,
                       CallbackFn&&       callback,
                       void*              handle = ll::sys_utils::getCurrentModuleHandle()
                   );
REMOTE_CALL_API CallbackFn const& importFunc(std::string const& nameSpace, std::string const& funcName);
REMOTE_CALL_API bool              hasFunc(std::string const& nameSpace, std::string const& funcName);
REMOTE_CALL_API bool              removeFunc(std::string const& nameSpace, std::string const& funcName);
REMOTE_CALL_API int               removeNameSpace(std::string const& nameSpace);
REMOTE_CALL_API int               removeFuncs(std::vector<std::pair<std::string, std::string>>& funcs);
REMOTE_CALL_API void              _onCallError(std::string const& msg, void* handle = ll::sys_utils::getCurrentModuleHandle());
#else
// ===== 代理模式：消费者插件通过 p_* 函数指针调用 MeowPAPI.dll 的 RC_* 代理 =====
// 这些指针在 DllLoader::load() 时通过 GetProcAddress 初始化。
// 指向 MeowPAPI.dll 中导出的 RC_* 包装函数。

// 强制链接器提取 MeowPAPI 静态库（DllLoader/EmbeddedData 依赖链）：
// MSVC 链接器按符号引用提取静态库目标文件。若消费者插件仅 include 本头
// 而未显式调用任何 meowpapi 函数（如仅用 RemoteCall 代理但不调用
// ensureLoaded），整个静态库会被 /OPT:REF 丢弃，导致 p_* 指针永远为空、
// DLL 数据不嵌入。/include 指令写入 .drectve 段由链接器无条件执行，
// 强制拉入 EnsureLoaded.obj → DllLoader.obj（含 EarlyDeployer）→ EmbeddedData.obj。
// 注意：消费者仍应在 enable() 里调用 meowpapi::ensureLoaded() 完成同步初始化。
namespace meowpapi {
bool ensureLoaded(); // 定义在静态库 EnsureLoaded.cpp
}
#if defined(_MSC_VER) && !defined(MEOWPAPI_DLL_EXPORTS)
#pragma comment(linker, "/include:?ensureLoaded@meowpapi@@YA_NXZ")
#endif

namespace detail {
    using ExportFuncFn      = bool(*)(std::string const&, std::string const&, CallbackFn&&, void*);
    using ImportFuncFn      = CallbackFn const&(*)(std::string const&, std::string const&);
    using HasFuncFn         = bool(*)(std::string const&, std::string const&);
    using RemoveFuncFn      = bool(*)(std::string const&, std::string const&);
    using RemoveNameSpaceFn = int(*)(std::string const&);
    using RemoveFuncsFn     = int(*)(std::vector<std::pair<std::string, std::string>>&);
    using OnCallErrorFn     = void(*)(std::string const&, void*);

    inline ExportFuncFn      p_exportFunc      = nullptr;
    inline ImportFuncFn      p_importFunc      = nullptr;
    inline HasFuncFn         p_hasFunc         = nullptr;
    inline RemoveFuncFn      p_removeFunc      = nullptr;
    inline RemoveNameSpaceFn p_removeNameSpace = nullptr;
    inline RemoveFuncsFn     p_removeFuncs     = nullptr;
    inline OnCallErrorFn     p_onCallError     = nullptr;

    // 空回调（本地定义，替代原版的 EMPTY_FUNC）
    inline CallbackFn const EMPTY_FUNC{};
} // namespace detail

// 包装函数：通过函数指针调用 MeowPAPI.dll 中的代理
inline bool exportFunc(
    std::string const& nameSpace,
    std::string const& funcName,
    CallbackFn&&       callback,
    void*              handle = ll::sys_utils::getCurrentModuleHandle()
) {
    if (detail::p_exportFunc) {
        return detail::p_exportFunc(nameSpace, funcName, std::move(callback), handle);
    }
    return false;
}

inline CallbackFn const& importFunc(std::string const& nameSpace, std::string const& funcName) {
    if (detail::p_importFunc) {
        return detail::p_importFunc(nameSpace, funcName);
    }
    return detail::EMPTY_FUNC;
}

inline bool hasFunc(std::string const& nameSpace, std::string const& funcName) {
    if (detail::p_hasFunc) {
        return detail::p_hasFunc(nameSpace, funcName);
    }
    return false;
}

inline bool removeFunc(std::string const& nameSpace, std::string const& funcName) {
    if (detail::p_removeFunc) {
        return detail::p_removeFunc(nameSpace, funcName);
    }
    return false;
}

inline int removeNameSpace(std::string const& nameSpace) {
    if (detail::p_removeNameSpace) {
        return detail::p_removeNameSpace(nameSpace);
    }
    return 0;
}

inline int removeFuncs(std::vector<std::pair<std::string, std::string>>& funcs) {
    if (detail::p_removeFuncs) {
        return detail::p_removeFuncs(funcs);
    }
    return 0;
}

inline void _onCallError(std::string const& msg, void* handle = ll::sys_utils::getCurrentModuleHandle()) {
    if (detail::p_onCallError) {
        detail::p_onCallError(msg, handle);
    }
}
#endif // MEOWPAPI_DLL_EXPORTS

inline ValueType _expandArg(std::vector<ValueType>& args, int& index) { return std::move(args[--index]); }

template <typename RTN, typename... Args>
inline bool
_exportAs(std::string const& nameSpace, std::string const& funcName, std::function<RTN(Args...)>&& callback) {
    CallbackFn cb = [callback = std::move(callback)](std::vector<ValueType> args) -> ValueType {
        if (sizeof...(Args) != args.size()) return {};
        int index = sizeof...(Args);
        if constexpr (std::is_void_v<RTN>) {
            callback(extract<Args>(_expandArg(args, index))...);
            return {};
        } else {
            return pack(callback(extract<Args>(_expandArg(args, index))...));
        }
    };
    return exportFunc(nameSpace, funcName, std::move(cb), ll::sys_utils::getCurrentModuleHandle());
}

template <typename RTN, typename... Args>
inline bool _importAs(std::string const& nameSpace, std::string const& funcName, std::function<RTN(Args...)>& func) {
    func = [nameSpace, funcName](Args... args) -> RTN {
        auto& rawFunc = importFunc(nameSpace, funcName);
        if (!rawFunc) {
            _onCallError(fmt::format("Fail to import! Function [{}::{}] has not been exported", nameSpace, funcName));
            return RTN();
        }
        std::vector<ValueType> params = {pack(std::forward<Args>(args))...};
        ValueType&&            res    = rawFunc(std::move(params));
        return extract<RTN>(std::move(res));
    };
    return true;
}

template <typename CB, typename Func = std::conditional_t<std::is_function_v<CB>, std::function<CB>, CB>>
inline Func importAs(std::string const& nameSpace, std::string const& funcName) {
    Func callback{};
    _importAs(nameSpace, funcName, callback);
    return std::move(callback);
}

template <typename CB>
inline bool exportAs(std::string const& nameSpace, std::string const& funcName, CB&& callback) {
    return _exportAs(nameSpace, funcName, std::function(std::forward<CB>(callback)));
}

} // namespace RemoteCall
