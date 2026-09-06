#pragma once
// LC3 (PHASE 14.5) — descriptor snapshot 과 그 무결성 검사.
//
// registry 자체(이름→핸들러)는 아직 `ConsoleCommandSystem.cpp` 의 `ConsoleCmd`
// 안에 있다. 그것을 옮기는 것은 LC6 의 domain 분리 몫이다. 여기서는 **schema
// 쪽만** 먼저 정본으로 세운다 — help 와 discovery 가 그것을 읽고, 무결성 검사가
// 그것을 지킨다.

#include "CommandDescriptor.h"

#include <string>
#include <string_view>
#include <vector>

namespace CommandCore
{
    class CommandRegistry
    {
    public:
        static CommandRegistry& Get();
        static CommandRegistry& Commandlets();

        /// 등록 하나를 기록한다. 실패 사유는 `Problems()` 에 쌓인다.
        void Add(CommandDescriptor descriptor);

        /// 조회 표에 들어가지 못한 이름을 기록한다.
        ///
        /// ★ 이것이 없으면 중복 검사가 **죽은 코드**다.
        ///
        ///   이름 충돌은 조회 표(`unordered_map::emplace`)에서 일어나고, 진 이름은
        ///   `Add` 에 오는 descriptor 에 애초에 담기지 않는다. 그래서 `Add` 안의
        ///   충돌 검사는 구조적으로 발화할 수 없었다 — 검사가 있는데 검사가 못
        ///   보는 상태다. 진 이름을 여기로 따로 넘겨야 `commands.selftest` 가
        ///   "조용히 한쪽이 먹혔다"를 실제로 잡는다.
        void RecordRejectedName(std::string_view name, std::string_view canonical);

        /// 이름으로 정렬한 snapshot. **순서가 결정적이다** — 등록 순서나
        /// 해시 순회에 기대면 실행마다 discovery 출력이 흔들리고, 그러면
        /// 소비자가 diff 로 비교할 수 없다(§13 LC3 완료 기준).
        const std::vector<CommandDescriptor>& Sorted() const;

        /// canonical 또는 alias 로 찾는다. 없으면 nullptr.
        const CommandDescriptor* Find(std::string_view name) const;

        /// 초기화 중 발견한 무결성 위반.
        ///
        /// 예전에는 이름 중복이 `printf` 한 줄로 지나갔고, 그 뒤로 조용히 한쪽이
        /// 먹혔다. 이제는 목록으로 남고 selftest 가 그것을 판정한다.
        const std::vector<std::string>& Problems() const noexcept { return m_problems; }

        std::size_t CommandCount() const noexcept { return m_descriptors.size(); }
        std::size_t NameCount() const noexcept;

    private:
        CommandRegistry() = default;

        CommandRegistry(const CommandRegistry&)            = delete;
        CommandRegistry& operator=(const CommandRegistry&) = delete;

        std::vector<CommandDescriptor>          m_descriptors;
        std::vector<std::string>                m_problems;

        mutable std::vector<CommandDescriptor>  m_sortedCache;
        mutable bool                            m_sortedDirty{ true };
    };

    /// descriptor 에서 help 문서를 만든다. 손으로 쓴 목록은 없다.
    std::string RenderHelp(const CommandRegistry& registry);

    /// 명령 하나의 상세.
    std::string RenderCommandDetail(const CommandDescriptor& descriptor);

    /// discovery 용 TSV. `GET /commands`(LC4)가 같은 snapshot 을 JSON 으로 낸다.
    ///
    /// 소비자가 C++ 소스를 긁는 것을 여기서 끝낸다(§2.4). 오늘 그 소비자는
    /// 10 개이고 `Invoke-Dx12Suite.ps1` 이 대표다.
    std::string RenderDiscoveryTsv(const CommandRegistry& registry);
}
