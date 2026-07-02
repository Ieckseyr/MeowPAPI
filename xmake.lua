-- MeowPAPI - PlaceholderAPI 注册中心静态库
-- 供 MeowSidebar 和 MeowMenu 共享链接
add_rules("mode.debug", "mode.release")

-- 优先使用本地缓存的 liteldev-repo，回退到 GitHub 远程
local local_repo = path.join(os.projectdir(), ".xmake", os.host(), os.arch(), "repositories", "liteldev-repo")
if os.exists(local_repo) then
    add_repositories("liteldev-repo " .. local_repo)
else
    add_repositories("liteldev-repo https://github.com/LiteLDev/xmake-repo.git")
end

if is_config("target_type", "server") then
    add_requires("levilamina 26.10.3", {configs = {target_type = "server"}})
else
    add_requires("levilamina 26.10.3", {configs = {target_type = "client"}})
end

add_requires("levibuildscript")
add_requires("legacyremotecall main", {configs = {target_type = "server"}})
add_requires("magic_enum v0.9.7")

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

option("target_type")
    set_default("server")
    set_showmenu(true)
    set_values("server", "client")
option_end()

target("MeowPAPI")
    add_cxflags("/EHa", "/utf-8", "/W4", "/Zm2000", "/wd4100", {force = true})
    add_defines("NOMINMAX", "UNICODE", "_AMD64_")
    add_packages("levilamina")
    add_packages("legacyremotecall")
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

    -- 源文件
    add_files("src/PlaceholderRegistry.cpp")
    add_files("src/PlaceholderApi.cpp")
    add_files("src/Builtins.cpp")
    add_files("src/RemoteCallBridge.cpp")
    add_files("src/BepApiBridge.cpp")

    add_includedirs("include")
    set_symbols("hidden")

    if is_config("target_type", "server") then
        add_defines("LL_PLAT_S")
    else
        add_defines("LL_PLAT_C")
    end
