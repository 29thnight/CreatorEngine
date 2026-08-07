#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedLivePipelineDesc.h"

#include <unordered_set>

bool LivePipelineDesc::Validate(std::string& outError) const
{
    // 발행된 슬롯의 집합. 노드 순서대로 걸으며 자란다 — 순서가 곧 계약이므로
    // 앞에서 발행되지 않은 것을 뒤가 읽으면 그것이 곧 순서 실수다.
    std::unordered_set<std::string> published;

    // 슬롯을 처음 발행한 노드. 중복 발행을 잡을 때 '누구와 부딪혔는지'를
    // 말해 주기 위해 든다 — 이름 하나만으로는 어디를 고쳐야 할지 모른다.
    std::unordered_map<std::string, std::string> publisher;

    for (const LivePassNode& node : m_nodes)
    {
        if (node.name.empty())
        {
            outError = "이름 없는 노드가 있다";
            return false;
        }

        const bool canBeInactive = static_cast<bool>(node.active);

        if (canBeInactive && !node.writes.empty())
        {
            outError = "노드 '" + node.name + "'은 꺼질 수 있는데 슬롯 '"
                + node.writes.front() + "'을(를) 발행한다 — 꺼진 프레임에"
                " 소비자가 무효 핸들을 받는다. modifies로 적어야 한다";
            return false;
        }

        for (const std::string& slot : node.reads)
        {
            if (published.end() == published.find(slot))
            {
                outError = "노드 '" + node.name + "'이 아직 발행되지 않은 슬롯 '"
                    + slot + "'을(를) 읽는다";
                return false;
            }
        }

        for (const std::string& slot : node.modifies)
        {
            if (published.end() == published.find(slot))
            {
                outError = "노드 '" + node.name + "'이 아직 발행되지 않은 슬롯 '"
                    + slot + "'을(를) 수정한다";
                return false;
            }
        }

        for (const std::string& slot : node.writes)
        {
            const auto found = publisher.find(slot);
            if (publisher.end() != found)
            {
                outError = "슬롯 '" + slot + "'을(를) 노드 '" + found->second
                    + "'와(과) '" + node.name + "'이 둘 다 발행한다 —"
                    " 뒤엣것은 수정이므로 modifies로 적어야 한다";
                return false;
            }
            published.insert(slot);
            publisher.emplace(slot, node.name);
        }

        if (!node.declare)
        {
            outError = "노드 '" + node.name + "'에 declare가 없다";
            return false;
        }
    }

    return true;
}

bool LivePipelineDesc::InitializeAll(const EnhancedFrameContext& context,
    uint32_t viewCount, std::string& outError) const
{
    for (const LivePassNode& node : m_nodes)
    {
        const uint32_t rounds = node.perView ? viewCount : 1u;
        for (uint32_t view = 0; view < rounds; ++view)
        {
            if (node.initialize)
            {
                if (!node.initialize(context, outError, view))
                {
                    outError = "노드 '" + node.name + "' 초기화 실패: " + outError;
                    return false;
                }
                continue;
            }

            // 패스가 없는 노드(live_present)는 초기화할 것이 없다.
            if (!node.instance) continue;

            EnhancedRenderPass* pass = node.instance(view);
            if (nullptr == pass) continue;

            if (!pass->Initialize(context, outError))
            {
                outError = "노드 '" + node.name + "' 초기화 실패: " + outError;
                return false;
            }
        }
    }
    return true;
}

bool LivePipelineDesc::PrepareAll(const EnhancedFrameContext& context,
    uint32_t viewIndex, std::string& outError) const
{
    for (const LivePassNode& node : m_nodes)
    {
        if (node.prepare)
        {
            if (!node.prepare(context, outError, viewIndex))
            {
                outError = "노드 '" + node.name + "' 프레임 준비 실패: " + outError;
                return false;
            }
            continue;
        }

        if (!node.instance) continue;

        EnhancedRenderPass* pass = node.instance(viewIndex);
        if (nullptr == pass) continue;

        if (!pass->PrepareFrame(context, outError))
        {
            outError = "노드 '" + node.name + "' 프레임 준비 실패: " + outError;
            return false;
        }
    }
    return true;
}

void LivePipelineDesc::ShutdownAll(uint32_t viewCount) const
{
    // 역순. 뒤에 선 것이 앞엣것의 자원을 참조할 수 있으므로 푸는 순서는
    // 세운 순서의 반대다 — 이것을 사람이 두 곳에 나눠 적던 자리가 접점 4였다.
    for (auto it = m_nodes.rbegin(); it != m_nodes.rend(); ++it)
    {
        const LivePassNode& node = *it;

        if (node.shutdown)
        {
            node.shutdown();
            continue;
        }

        if (!node.instance) continue;

        const uint32_t rounds = node.perView ? viewCount : 1u;
        for (uint32_t view = 0; view < rounds; ++view)
        {
            if (EnhancedRenderPass* pass = node.instance(view)) pass->Shutdown();
        }
    }
}

void LivePipelineDesc::DeclareAll(LiveBlackboard& blackboard, EnhancedRenderGraph& graph,
    const EnhancedFrameContext& context, const LiveFrameBinding& binding) const
{
    for (const LivePassNode& node : m_nodes)
    {
        // 비활성 노드는 통째로 건너뛴다. modifies 슬롯은 값이 그대로 남으므로
        // 뒤 노드가 이전 값을 이어받는다 — 폴백 코드가 필요 없는 이유다.
        if (!node.IsActive()) continue;
        node.declare(blackboard, graph, context, binding);
    }
}

std::string LivePipelineDesc::Dump() const
{
    const auto joinSlots = [](const std::vector<std::string>& slots) -> std::string
    {
        std::string joined;
        for (size_t i = 0; i < slots.size(); ++i)
        {
            if (0 != i) joined += ", ";
            joined += slots[i];
        }
        return joined;
    };

    std::string text = "LivePipelineDesc — 노드 " + std::to_string(m_nodes.size()) + "개\n";
    for (size_t i = 0; i < m_nodes.size(); ++i)
    {
        const LivePassNode& node = m_nodes[i];

        text += "  " + std::to_string(i) + ". " + node.name;
        if (node.active) text += node.IsActive() ? "  [켜짐]" : "  [꺼짐]";
        text += "\n";

        if (!node.reads.empty())    text += "       읽기: " + joinSlots(node.reads) + "\n";
        if (!node.writes.empty())   text += "       발행: " + joinSlots(node.writes) + "\n";
        if (!node.modifies.empty()) text += "       수정: " + joinSlots(node.modifies) + "\n";
    }
    return text;
}

#endif
