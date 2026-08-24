// Commands.cpp - /meowpapi 指令注册
//
// 命令格式：
//   /meowpapi              - 显示帮助
//   /meowpapi version      - 查询版本信息（所有人可用）
//   /meowpapi list [页码]  - 分页查看占位符列表（OP，execute 内自检）
//
// 权限说明：命令注册为 CommandPermissionLevel::Any（避免 Bedrock 客户端
// 对 GameDirectors 命令的本地隐藏/未知命令问题，MeowMenu 同款方案），
// list 子命令在 execute 内用 getCommandPermissionLevel() >= 1 自检 OP。
//
// 执行来源：玩家 → 聊天栏彩色输出；控制台/后台（非玩家来源）→
// output.success 纯文本输出（去除颜色代码），后台指令同样可用。
#include "meowpapi/PlaceholderRegistry.h"
#include "meowpapi/DllExports.h"
#include "lse/LseBridge.h"

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"

#include <ctime>
#include <fmt/format.h>
#include <string>
#include <vector>

namespace meowpapi {

namespace {

// PE TimeDateStamp → "YYYY-MM-DD HH:MM" 可读时间
std::string formatBuildTime(uint64_t ts) {
    if (ts == 0) return "unknown";
    time_t    t = static_cast<time_t>(ts);
    struct tm tmv;
    localtime_s(&tmv, &t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv);
    return buf;
}

// 去除 §x 颜色代码（控制台输出用；§ 为 UTF-8 0xC2 0xA7，后跟 1 位代码字符）
std::string stripColors(std::string const& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (i + 2 < s.size() && static_cast<unsigned char>(s[i]) == 0xC2
            && static_cast<unsigned char>(s[i + 1]) == 0xA7) {
            i += 3; // 跳过 § + 代码字符
        } else {
            out += s[i++];
        }
    }
    return out;
}

// 分发输出：玩家走聊天栏（保留颜色），控制台/后台走 output.success（纯文本）
void dispatchLines(Player* player, CommandOutput& output, std::vector<std::string> const& lines) {
    if (player) {
        for (auto const& line : lines) player->sendMessage(line);
    } else {
        for (auto const& line : lines) output.success(stripColors(line));
    }
}

constexpr int kListPageSize = 8; // /meowpapi list 每页条数

struct PageParam {
    int page = 1;
};

// 构建版本信息行（/meowpapi version 与帮助共用）
std::vector<std::string> buildVersionLines() {
    std::vector<std::string> lines;
    lines.push_back("§a===== §bMeowPAPI §a=====");
    lines.push_back(fmt::format(
        "§e版本: §f{} §7(ABI 0x{:06x})", MEOWPAPI_VERSION_STRING, MEOWPAPI_ABI_VERSION));
    lines.push_back(fmt::format("§e构建: §f{}", formatBuildTime(MeowPAPI_GetBuildTimestamp())));
    uint32_t features = MEOWPAPI_ABI_FEATURE_PARAMS;
    lines.push_back(fmt::format(
        "§e功能: §f{}",
        (features & MEOWPAPI_ABI_FEATURE_PARAMS) ? "带参占位符(GMLIB PAPI 兼容)" : "基础"));
    // lrca 挂载细节诊断（模块名/精确 or 模糊匹配/符号不匹配）——
    // 云测试服等远端环境秒判 LSE 桥状态的关键信息
    lines.push_back(fmt::format("§elrca: §f{}", lse::statusText()));
    auto count = PlaceholderRegistry::getInstance().listPlaceholders().size();
    lines.push_back(fmt::format(
        "§e占位符: §f{} 个 §7(/meowpapi list 查看)", count));
    return lines;
}

// 构建帮助行
std::vector<std::string> buildHelpLines() {
    return {
        "§e/meowpapi version §7- 查看版本信息",
        "§e/meowpapi list [页码] §7- 占位符列表(OP)",
    };
}

// 构建占位符分页列表行（仅 OP 调用）
std::vector<std::string> buildListLines(int page) {
    std::vector<std::string> lines;
    auto infos = PlaceholderRegistry::getInstance().listPlaceholderInfos();
    if (infos.empty()) {
        lines.push_back("§7[MeowPAPI] 暂无已注册占位符");
        return lines;
    }
    int totalPages =
        static_cast<int>((infos.size() + kListPageSize - 1) / kListPageSize);
    if (page < 1) page = 1;
    if (page > totalPages) page = totalPages;

    lines.push_back(fmt::format(
        "§a===== §b占位符列表 §7[{}/{}]§a===== §f{} 个",
        page, totalPages, infos.size()));

    int start = (page - 1) * kListPageSize;
    int end   = (std::min)(start + kListPageSize, static_cast<int>(infos.size()));
    for (int i = start; i < end; i++) {
        auto const& info     = infos[i];
        char const* typeTag  = info.type == PlaceholderType::Server ? "Server"
                               : info.type == PlaceholderType::Player ? "Player"
                                                                         : "Static";
        char const* paramTag = info.hasParams ? " §6[带参]" : "";
        lines.push_back(fmt::format(
            "§7#{} §f{} §7[{}] §8{}{}", i + 1, info.name, typeTag, info.pluginName, paramTag));
    }
    if (totalPages > 1) {
        lines.push_back(fmt::format(
            "§7下一页: /meowpapi list {}", (page % totalPages) + 1));
    }
    return lines;
}

} // anonymous namespace

void registerCommands() {
    auto& cmd = ll::command::CommandRegistrar::getInstance(false)
                    .getOrCreateCommand("meowpapi", "§aMeowPAPI §b占位符中心", CommandPermissionLevel::Any);

    // /meowpapi - 无参数，显示帮助
    cmd.overload().execute(
        [](CommandOrigin const& origin, CommandOutput& output) {
            auto* entity = origin.getEntity();
            auto* player = (entity && entity->isPlayer()) ? static_cast<Player*>(entity) : nullptr;
            dispatchLines(player, output, buildVersionLines());
            dispatchLines(player, output, buildHelpLines());
        }
    );

    // /meowpapi version - 版本信息（所有人；控制台可用）
    cmd.overload().text("version").execute(
        [](CommandOrigin const& origin, CommandOutput& output) {
            auto* entity = origin.getEntity();
            auto* player = (entity && entity->isPlayer()) ? static_cast<Player*>(entity) : nullptr;
            dispatchLines(player, output, buildVersionLines());
        }
    );

    // /meowpapi list [页码] - 占位符分页列表（玩家 OP 自检；控制台天然全权）
    cmd.overload<PageParam>().text("list").optional("page").execute(
        [](CommandOrigin const& origin, CommandOutput& output, PageParam const& param) {
            auto* entity = origin.getEntity();
            auto* player = (entity && entity->isPlayer()) ? static_cast<Player*>(entity) : nullptr;
            if (player && (int)player->getCommandPermissionLevel() < 1) {
                player->sendMessage("§c[MeowPAPI] 权限不足，list 仅 OP 可用");
                return;
            }
            dispatchLines(player, output, buildListLines(param.page));
        }
    );
}

} // namespace meowpapi
