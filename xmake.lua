-- MeowPAPI - PlaceholderAPI 注册中心
-- 架构：
--   1. MeowPAPI_DLL  - 独立 LeviLamina 插件 DLL（含实际逻辑）
--   2. MeowPAPI      - 静态库（中间层包装，嵌入 DLL 数据，检测/部署/加载/转发）
--
-- 工作流程：
--   - 开发者先执行 xmake build MeowPAPI_DLL 生成 embedded_dll.h 和 embedded_manifest.h
--   - 消费者插件 add_deps("MeowPAPI") 链接静态库
--   - 静态库将 MeowPAPI.dll 二进制嵌入消费者插件内部
--   - 消费者插件加载时，DllLoader 从嵌入数据释放 DLL 到 plugins/MeowPAPI/，
--     LoadLibraryW 加载并自动初始化（initAsServer + registerBuiltins + exportRC + installBep）
--   - 多消费者插件防冲突：std::call_once 保证只部署一次
--
-- 不再需要独立的 auto_deploy EXE，静态库自身完成部署。
add_rules("mode.debug", "mode.release")

-- 优先使用本地缓存的 liteldev-repo，回退到 GitHub 远程
local function find_local_repo()
    local candidates = {
        path.join(os.projectdir(), ".xmake", os.host(), os.arch(), "repositories", "liteldev-repo"),
        path.join(os.projectdir(), "..", ".xmake", os.host(), os.arch(), "repositories", "liteldev-repo"),
        path.join(os.projectdir(), "..", "RecipeExporter", ".xmake", os.host(), os.arch(), "repositories", "liteldev-repo"),
        path.join(os.projectdir(), "..", "MeowEntityModel", ".xmake", os.host(), os.arch(), "repositories", "liteldev-repo"),
        path.join(os.projectdir(), "..", "MeowSidebar", ".xmake", os.host(), os.arch(), "repositories", "liteldev-repo"),
    }
    for _, p in ipairs(candidates) do
        if os.exists(p) then return p end
    end
    return nil
end
local local_repo = find_local_repo()
if local_repo then
    add_repositories("liteldev-repo " .. local_repo)
else
    add_repositories("liteldev-repo https://github.com/LiteLDev/xmake-repo.git")
end

if is_config("target_type", "server") then
    add_requires("levilamina 26.10.14", {configs = {target_type = "server"}})
else
    add_requires("levilamina 26.10.14", {configs = {target_type = "client"}})
end

add_requires("levibuildscript")
add_requires("magic_enum v0.9.7")

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

option("target_type")
    set_default("server")
    set_showmenu(true)
    set_values("server", "client")
option_end()

--===== DLL 目标：MeowPAPI.dll（包含所有实际逻辑）=====
-- set_default(false): 避免被消费者插件 includes 后，xmake build -r 并行构建
-- 多个 linkrule 目标导致 prelink 共享 .prelink/lib/ 竞态（LNK1181）
-- 显式构建：xmake build MeowPAPI_DLL
target("MeowPAPI_DLL")
    set_default(false)
    add_rules("@levibuildscript/linkrule")
    add_cxflags("/EHa", "/utf-8", "/W4", "/Zm2000", "/wd4100", {force = true})
    add_defines("NOMINMAX", "UNICODE", "_AMD64_", "LL_MEMORY_OPERATORS", "MEOWPAPI_DLL_EXPORTS")
    add_packages("levilamina")
    add_packages("magic_enum")
    set_exceptions("on")
    set_kind("shared")
    set_languages("c++23")
    set_basename("MeowPAPI")
    add_shflags("/DELAYLOAD:bedrock_runtime.dll")
    add_ldflags("/OPT:REF", "/OPT:ICF", "/DEBUG:NONE")

    -- DLL 源文件（实际实现）
    add_files("src/PlaceholderRegistry.cpp")
    add_files("src/PlaceholderApi.cpp")
    add_files("src/Builtins.cpp")
    add_files("src/RemoteCallBridge.cpp")
    add_files("src/RemoteCallProxy.cpp")
    add_files("src/BepApiBridge.cpp")
    add_files("src/DllExports.cpp")
    add_files("src/DllMain.cpp")
    add_files("src/MemoryOperators.cpp")
    -- LeviLamina 插件入口（独立插件模式）
    add_files("src/PluginEntry.cpp")
    -- /meowpapi 指令（version 所有人 / list OP 自检）
    add_files("src/Commands.cpp")
    -- lrca 运行时可选挂载桥（软依赖：GetModuleHandleW + GetProcAddress
    -- 解析 mangled 符号，导入表无 LegacyRemoteCall.dll）
    add_files("src/lse/LseBridge.cpp")

    add_includedirs("include")
    add_includedirs("src")
    set_symbols("hidden")

    if is_config("target_type", "server") then
        add_defines("LL_PLAT_S")
    else
        add_defines("LL_PLAT_C")
    end

    -- after_build: 生成 embedded_dll.h、embedded_manifest.h 和 embedded_build_info.h 供静态库使用
    after_build(function (target)
        local scriptdir = os.scriptdir()
        local manifest = path.join(scriptdir, "manifest.json")
        local ps_script = path.join(scriptdir, "gen_embedded.ps1")
        local bi_script = path.join(scriptdir, "gen_build_info.ps1")

        -- 1. 生成 embedded_dll.h
        local dllpath = target:targetfile()
        local dll_out = path.join(scriptdir, "embedded_dll.h")
        os.exec(string.format('powershell -ExecutionPolicy Bypass -File "%s" -OutPath "%s" -VarName "embedded_dll_data" -DataFile "%s"',
            ps_script, dll_out, dllpath))

        -- 2. 生成 embedded_manifest.h
        if os.exists(manifest) then
            local mf_out = path.join(scriptdir, "embedded_manifest.h")
            os.exec(string.format('powershell -ExecutionPolicy Bypass -File "%s" -OutPath "%s" -VarName "embedded_manifest_data" -DataFile "%s"',
                ps_script, mf_out, manifest))
        end

        -- 3. 生成 embedded_build_info.h（PE TimeDateStamp，供 DllLoader 版本检查）
        local bi_out = path.join(scriptdir, "embedded_build_info.h")
        os.exec(string.format('powershell -ExecutionPolicy Bypass -File "%s" -DllPath "%s" -OutPath "%s"',
            bi_script, dllpath, bi_out))
    end)

--===== 静态库目标：MeowPAPI（中间层包装，嵌入 DLL 数据）=====
-- 静态库将 MeowPAPI.dll 二进制嵌入消费者插件内部。
-- 加载时自动释放 DLL 到 plugins/MeowPAPI/，LoadLibraryW 加载并自动初始化。
-- 不再需要外部 auto_deploy EXE。
target("MeowPAPI")
    add_cxflags("/EHa", "/utf-8", "/W4", "/Zm2000", "/wd4100", {force = true})
    add_defines("NOMINMAX", "UNICODE", "_AMD64_")
    add_packages("levilamina")
    add_packages("magic_enum")
    set_exceptions("on")
    set_kind("static")
    set_languages("c++23")

    -- 头文件
    add_headerfiles("include/meowpapi/PlaceholderRegistry.h")
    add_headerfiles("include/meowpapi/PlaceholderApi.h")
    add_headerfiles("include/meowpapi/Builtins.h")
    add_headerfiles("include/meowpapi/RemoteCallBridge.h")
    add_headerfiles("include/meowpapi/BepApiBridge.h")
    add_headerfiles("include/meowpapi/DllExports.h")
    add_headerfiles("include/meowpapi/RemoteCallAPI.h")
    add_headerfiles("include/meowpapi/EnsureLoaded.h")

    -- 包装层源文件（转发到 DLL，不含实际逻辑）
    add_files("src-wrapper/EmbeddedData.cpp")
    add_files("src-wrapper/DllLoader.cpp")
    add_files("src-wrapper/PlaceholderApi.cpp")
    add_files("src-wrapper/Builtins.cpp")
    add_files("src-wrapper/RemoteCallBridge.cpp")
    add_files("src-wrapper/BepApiBridge.cpp")
    add_files("src-wrapper/EnsureLoaded.cpp")

    add_includedirs("include")
    add_includedirs("src-wrapper")
    add_includedirs(".") -- for embedded_dll.h / embedded_manifest.h
    set_symbols("hidden")

    if is_config("target_type", "server") then
        add_defines("LL_PLAT_S")
    else
        add_defines("LL_PLAT_C")
    end

    -- 确保 embedded_dll.h、embedded_manifest.h 和 embedded_build_info.h 在编译前已生成
    -- 需先执行：xmake build MeowPAPI_DLL
    before_build(function (target)
        local scriptdir = os.scriptdir()
        local embedded_dll = path.join(scriptdir, "embedded_dll.h")
        local embedded_mf = path.join(scriptdir, "embedded_manifest.h")
        local embedded_bi = path.join(scriptdir, "embedded_build_info.h")
        if not os.exists(embedded_dll) or not os.exists(embedded_mf) or not os.exists(embedded_bi) then
            raise("embedded_dll.h/embedded_manifest.h/embedded_build_info.h not found! Run 'xmake build MeowPAPI_DLL' first.")
        end
    end)
