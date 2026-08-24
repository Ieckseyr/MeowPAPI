// EmbeddedData.cpp - 嵌入的 MeowPAPI.dll 和 manifest.json 数据
//
// 此文件编译到静态库中，将 MeowPAPI.dll 的二进制数据和 manifest.json
// 嵌入到消费者插件的 DLL 内部。
//
// embedded_dll.h、embedded_manifest.h 和 embedded_build_info.h 由 MeowPAPI_DLL
// target 的 after_build 步骤生成（通过 gen_embedded.ps1 和 gen_build_info.ps1）。
//
// 消费者插件加载时，DllLoader 从这些数据中释放 DLL 到磁盘并加载。
// 版本检查：比较嵌入的 kEmbeddedBuildTimestamp 与已部署 DLL 的 PE TimeDateStamp，
// 若已部署版本过期则自动替换文件。
#include "embedded_dll.h"
#include "embedded_manifest.h"
#include "embedded_build_info.h"

#include <cstdint>
#include <cstddef>

namespace meowpapi {

// 嵌入的 MeowPAPI.dll 数据
const uint8_t* getEmbeddedDllData() {
    return embedded_dll_data.data();
}

size_t getEmbeddedDllSize() {
    return embedded_dll_data.size();
}

// 嵌入的 manifest.json 数据
const uint8_t* getEmbeddedManifestData() {
    return embedded_manifest_data.data();
}

size_t getEmbeddedManifestSize() {
    return embedded_manifest_data.size();
}

// 嵌入的 MeowPAPI.dll 构建时间戳（PE TimeDateStamp）
// 由 gen_build_info.ps1 在构建时从 DLL 文件提取
uint32_t getEmbeddedBuildTimestamp() {
    return kEmbeddedBuildTimestamp;
}

} // namespace meowpapi
