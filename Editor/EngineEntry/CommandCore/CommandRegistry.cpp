#include "CommandRegistry.h"

#include <algorithm>
#include <map>

namespace CommandCore
{
    std::string_view ToString(CommandCost cost) noexcept
    {
        switch (cost)
        {
        case CommandCost::Immediate: return "immediate";
        case CommandCost::Frames:    return "frames";
        case CommandCost::Long:      return "long";
        }
        return "unknown";
    }

    std::string_view ToString(CommandRoles roles) noexcept
    {
        switch (roles)
        {
        case CommandRoles::Editor: return "editor";
        case CommandRoles::Player: return "player";
        case CommandRoles::Both:   return "both";
        }
        return "unknown";
    }

    std::string_view ToString(CommandClass cls) noexcept
    {
        switch (cls)
        {
        case CommandClass::EditorOperation: return "editor_operation";
        case CommandClass::EngineService:   return "engine_service";
        case CommandClass::Probe:           return "probe";
        case CommandClass::RawFixture:      return "raw_fixture";
        }
        return "unknown";
    }

    std::string_view ToString(CommandLiveness liveness) noexcept
    {
        switch (liveness)
        {
        case CommandLiveness::Live:              return "live";
        case CommandLiveness::RequiresRestart:   return "requires_restart";
        case CommandLiveness::TerminatesProcess: return "terminates_process";
        }
        return "unknown";
    }

    CommandRegistry& CommandRegistry::Get()
    {
        static CommandRegistry registry;
        return registry;
    }

    void CommandRegistry::Add(CommandDescriptor descriptor)
    {
        m_sortedDirty = true;

        if (descriptor.canonical.empty())
        {
            m_problems.push_back("이름 없는 명령이 등록됐다");
            return;
        }

        // ★ 요약이 없으면 등록을 거부한다.
        //
        //   서명이 요구하지 않으면 아무도 안 쓴다 — 78 개가 help 에 없던 이유다.
        //   여기서 막지 않으면 다음 명령이 같은 자리에 다시 쌓인다.
        if (descriptor.summary.empty())
        {
            m_problems.push_back("요약이 없는 명령: " + descriptor.canonical);
            return;
        }

        const auto conflicts = [this](const std::string& name) -> bool
        {
            for (const CommandDescriptor& existing : m_descriptors)
            {
                if (existing.canonical == name) return true;
                if (std::find(existing.aliases.begin(), existing.aliases.end(), name)
                    != existing.aliases.end()) return true;
            }
            return false;
        };

        if (conflicts(descriptor.canonical))
        {
            m_problems.push_back("이름 중복: " + descriptor.canonical);
            return;
        }

        // ★ 별칭 충돌도 canonical 과 **같게** 다룬다.
        //
        //   처음에는 문제만 적고 descriptor 를 그대로 저장했다. 그러면 `Find`
        //   가 선형 탐색으로 먼저 등록된 쪽을 돌려주므로, 충돌한 별칭으로는
        //   영영 닿을 수 없는 descriptor 가 표에 남는다 — 이 클래스가 없애겠다고
        //   한 "조용히 한쪽이 먹힌다"를 이름만 바꿔 재현하는 셈이다.
        for (const std::string& alias : descriptor.aliases)
        {
            if (conflicts(alias))
            {
                m_problems.push_back("별칭 중복: " + alias + " (" + descriptor.canonical + ")");
                return;
            }
        }

        descriptor.registrationIndex = m_descriptors.size();
        m_descriptors.push_back(std::move(descriptor));
    }

    void CommandRegistry::RecordRejectedName(std::string_view name, std::string_view canonical)
    {
        m_problems.push_back("이름 중복으로 등록되지 못함: " + std::string(name)
                             + " (" + std::string(canonical) + ")");
    }

    const std::vector<CommandDescriptor>& CommandRegistry::Sorted() const
    {
        if (m_sortedDirty)
        {
            m_sortedCache = m_descriptors;
            std::sort(m_sortedCache.begin(), m_sortedCache.end(),
                      [](const CommandDescriptor& a, const CommandDescriptor& b)
                      { return a.canonical < b.canonical; });
            m_sortedDirty = false;
        }
        return m_sortedCache;
    }

    const CommandDescriptor* CommandRegistry::Find(std::string_view name) const
    {
        for (const CommandDescriptor& descriptor : m_descriptors)
        {
            if (descriptor.canonical == name) return &descriptor;
            for (const std::string& alias : descriptor.aliases)
            {
                if (alias == name) return &descriptor;
            }
        }
        return nullptr;
    }

    std::size_t CommandRegistry::NameCount() const noexcept
    {
        std::size_t count = 0;
        for (const CommandDescriptor& descriptor : m_descriptors)
        {
            count += 1 + descriptor.aliases.size();
        }
        return count;
    }

    namespace
    {
        std::string NameColumn(const CommandDescriptor& descriptor)
        {
            std::string column = descriptor.canonical;
            for (const std::string& alias : descriptor.aliases)
            {
                column += "|" + alias;
            }
            if (!descriptor.usage.empty()) column += " " + descriptor.usage;
            return column;
        }

        /// 화면 폭 기준 정렬. 한글은 UTF-8 에서 3바이트인데 화면에서는 2칸이라,
        /// 바이트 길이로 맞추면 표가 어긋난다.
        std::size_t DisplayWidth(std::string_view text)
        {
            std::size_t width = 0;
            for (std::size_t i = 0; i < text.size();)
            {
                const unsigned char c = static_cast<unsigned char>(text[i]);
                if (c < 0x80)      { width += 1; i += 1; }
                // 뒤따름 바이트(0x80~0xBF)가 선행 바이트 자리에 오면 잘못된
                // UTF-8 이다. 2바이트 문자로 오해하면 폭과 진행이 함께 어긋나
                // 표 전체가 밀린다. 한 칸으로 세고 한 바이트만 건넌다.
                else if (c < 0xC0) { width += 1; i += 1; }
                else if (c < 0xE0) { width += 1; i += 2; }
                else if (c < 0xF0) { width += 2; i += 3; }
                else               { width += 2; i += 4; }
            }
            return width;
        }
    }

    std::string RenderHelp(const CommandRegistry& registry)
    {
        const std::vector<CommandDescriptor>& sorted = registry.Sorted();

        // 이름 열 폭을 실제 내용에서 정한다. 손으로 맞춘 공백은 명령을 하나
        // 더할 때마다 어긋나고, 어긋난 표는 아무도 다시 맞추지 않는다.
        std::size_t nameWidth = 0;
        for (const CommandDescriptor& descriptor : sorted)
        {
            nameWidth = (std::max)(nameWidth, DisplayWidth(NameColumn(descriptor)));
        }
        nameWidth = (std::min<std::size_t>)(nameWidth, 46);

        std::string help = "\n[CLI] 사용 가능한 명령 (";
        help += std::to_string(registry.CommandCount());
        help += "개 · 이름 ";
        help += std::to_string(registry.NameCount());
        help += "개)\n\n";

        for (const CommandDescriptor& descriptor : sorted)
        {
            const std::string column = NameColumn(descriptor);
            help += "  ";
            help += column;

            const std::size_t width = DisplayWidth(column);
            help.append((width < nameWidth) ? (nameWidth - width + 2) : 2, ' ');
            help += descriptor.summary;
            help += '\n';
        }

        help +=
            "\n실행 인자: --exec \"<명령>\"  |  --script <파일>  |  --console\n"
            "           --exec-args <명령> <인자>... [--]  라인 문법을 거치지 않는 구조화 입력\n"
            "           --fail-fast  첫 실패에서 남은 명령을 버리고 종료한다\n"
            "           --heapcheck  CRT 디버그 힙 전수 검사(매우 느림)\n"
            "\n공백이 든 이름·값은 따옴표로 감싼다: object.parent \"Big Boss\" \"Main Characters\"\n"
            "명령 하나의 상세는 'help <이름>' 또는 'commands.describe <이름>'.\n\n";
        return help;
    }

    std::string RenderCommandDetail(const CommandDescriptor& descriptor)
    {
        std::string detail = "\n[CLI] " + descriptor.canonical;
        if (!descriptor.usage.empty()) detail += " " + descriptor.usage;
        detail += "\n  요약   : " + descriptor.summary + "\n";

        if (!descriptor.aliases.empty())
        {
            detail += "  별칭   : ";
            for (std::size_t i = 0; i < descriptor.aliases.size(); ++i)
            {
                if (0 != i) detail += ", ";
                detail += descriptor.aliases[i];
            }
            detail += "\n";
        }

        detail += "  비용   : ";
        detail += ToString(descriptor.cost);
        detail += "\n  역할   : ";
        detail += ToString(descriptor.roles);
        detail += "\n  결과   : ";
        detail += descriptor.resultBearing ? "CommandResult" : "미이행(LegacyUnreported)";
        detail += "\n\n";
        return detail;
    }

    std::string RenderDiscoveryTsv(const CommandRegistry& registry)
    {
        std::string tsv = "# lc3-commands v1\n";
        tsv += "# commands\t" + std::to_string(registry.CommandCount()) + "\n";
        tsv += "# names\t" + std::to_string(registry.NameCount()) + "\n";
        tsv += "# problems\t" + std::to_string(registry.Problems().size()) + "\n";
        for (const std::string& problem : registry.Problems())
        {
            tsv += "# problem\t" + problem + "\n";
        }
        // ★ 새 열은 **끝이 아니라 `usage` 앞**에 넣는다. `summary` 는 자유 문장이라
        //   맨 뒤여야 하고(탭이 섞일 여지가 가장 큰 열이다), 분류는 기계가 읽는
        //   값이므로 앞쪽 고정 폭 구역에 모아 둔다. 소비자는 열 이름으로 읽는다.
        tsv += "canonical\taliases\tcost\troles\tclass\tliveness\tresult_bearing\tusage\tsummary\n";

        for (const CommandDescriptor& descriptor : registry.Sorted())
        {
            std::string aliases;
            for (const std::string& alias : descriptor.aliases)
            {
                if (!aliases.empty()) aliases.push_back(',');
                aliases += alias;
            }
            if (aliases.empty()) aliases = "-";

            tsv += descriptor.canonical;
            tsv += '\t'; tsv += aliases;
            tsv += '\t'; tsv += ToString(descriptor.cost);
            tsv += '\t'; tsv += ToString(descriptor.roles);
            tsv += '\t'; tsv += ToString(descriptor.cls);
            tsv += '\t'; tsv += ToString(descriptor.liveness);
            tsv += '\t'; tsv += descriptor.resultBearing ? "yes" : "no";
            tsv += '\t'; tsv += descriptor.usage.empty() ? "-" : descriptor.usage;
            tsv += '\t'; tsv += descriptor.summary;
            tsv += '\n';
        }
        return tsv;
    }
}
