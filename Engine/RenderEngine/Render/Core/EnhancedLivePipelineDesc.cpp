#include "EnhancedLivePipelineDesc.h"
#include "../../../EngineDiagnostics/ProfileScope.h"

#include <stdio.h>
#include "../../../Utility_Framework/StandardLibrary.h"

namespace
{
    LivePassNode MakeTestNode(std::string name)
    {
        LivePassNode node{};
        node.name = std::move(name);
        node.declare = [](LiveBlackboard&, EnhancedRenderGraph&,
            const EnhancedFrameContext&, const LiveFrameBinding&) {};
        return node;
    }

    bool ContainsAll(const std::string& text,
        std::initializer_list<const char*> needles)
    {
        for (const char* needle : needles)
        {
            if (std::string::npos == text.find(needle)) return false;
        }
        return true;
    }
}

void LivePipelineDesc::AddNode(LivePassNode node)
{
#if !CE_SHIPPING
    node.declarationMarker = ce::intern_runtime_marker("RenderDeclare." + node.name, ce::marker_kind::cpu_scope);
#endif
    m_nodes.push_back(std::move(node));
}

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
            static const bool trace = [] {
                char* value = nullptr;
                std::size_t size = 0;
                if (_dupenv_s(&value, &size, "CE_RENDER_PROGRESS_TRACE") != 0) return false;
                const bool enabled = value && std::string_view(value) == "1";
                std::free(value);
                return enabled;
            }();
            if (trace)
            {
                std::printf("[render.pass.progress] phase=initialize.begin node=%s view=%u\n", node.name.c_str(), view);
                std::fflush(stdout);
            }


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
    const EnhancedFrameContext& context, const LiveFrameBinding& binding,
    const std::function<void(const LivePassNode&, const LiveBlackboard&)>& afterDeclare) const
{
    ce::profile_scope declarationProfile{ce::marker<"RenderGraphDeclare">()};
    graph.BeginDeclarationPackets();
    try
    {
        for (const LivePassNode& node : m_nodes)
        {
            // 비활성 노드는 통째로 건너뛴다. modifies 슬롯은 값이 그대로 남으므로
            // 뒤 노드가 이전 값을 이어받는다 — 폴백 코드가 필요 없는 이유다.
            if (!node.IsActive())
            {
                continue;
            }
            graph.NextDeclarationPacket();
            ce::profile_scope profile{node.declarationMarker};
            {
                ce::profile_scope body{ce::marker<"RenderGraphDeclareNodeBody">()};
                node.declare(blackboard, graph, context, binding);
            }
            if (afterDeclare)
            {
                ce::profile_scope callback{ce::marker<"RenderGraphDeclareAfterNode">()};
                afterDeclare(node, blackboard);
            }
        }
        graph.FinishDeclarationPackets();
    }
    catch (...)
    {
        graph.CancelDeclarationPackets();
        throw;
    }
}

bool LivePipelineDesc::HasConsumer(const std::string& slot, const EnhancedFrameContext& context,
    const LiveFrameBinding& binding) const
{
    for (const LivePassNode& node : m_nodes)
    {
        if (!node.IsActive()) continue;
        if (std::find(node.reads.begin(), node.reads.end(), slot) == node.reads.end()) continue;
        if (!node.consumes || node.consumes(slot, context, binding)) return true;
    }
    return false;
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

    std::string text = "LivePipelineDesc — " + std::to_string(m_nodes.size()) + " nodes\n";
    for (size_t i = 0; i < m_nodes.size(); ++i)
    {
        const LivePassNode& node = m_nodes[i];

        text += "  " + std::to_string(i) + ". " + node.name;
        if (node.active) text += node.IsActive() ? "  [active]" : "  [inactive]";
        text += "\n";

        if (!node.reads.empty())    text += "       reads: " + joinSlots(node.reads) + "\n";
        if (!node.writes.empty())   text += "       writes: " + joinSlots(node.writes) + "\n";
        if (!node.modifies.empty()) text += "       modifies: " + joinSlots(node.modifies) + "\n";
    }
    return text;
}

bool LivePipelineDesc::RunSelfTest(std::string& outLog)
{
    outLog.clear();
    constexpr uint32_t kSelfTestCases = 15;
    uint32_t passed = 0;
    uint32_t total = 0;

    const auto record = [&](bool ok, const char* label, const std::string& detail)
    {
        ++total;
        if (ok) ++passed;
        outLog += "[" + std::to_string(total) + "/" + std::to_string(kSelfTestCases) + "] " + label + " — "
            + (ok ? "통과" : "실패");
        if (!detail.empty()) outLog += " (" + detail + ")";
        outLog += "\n";
    };

    const auto expectFailure = [&](LivePipelineDesc desc, const char* label,
        std::initializer_list<const char*> expected)
    {
        std::string error;
        const bool rejected = !desc.Validate(error);
        const bool matched = rejected && ContainsAll(error, expected);
        record(matched, label, rejected ? error : "잘못된 기술을 허용함");
    };

    // 정상 계약과 Dump 계약은 같은 작은 기술을 공유한다. source가 처음
    // 발행하고, optional은 꺼져도 기존 값을 보존하도록 modifies만 가지며,
    // consumer가 그 결과를 읽어 새 슬롯을 발행한다.
    LivePipelineDesc valid;
    LivePassNode source = MakeTestNode("source");
    source.writes = { "scene" };
    valid.AddNode(std::move(source));

    LivePassNode optional = MakeTestNode("optional");
    optional.modifies = { "scene" };
    optional.active = [] { return false; };
    valid.AddNode(std::move(optional));

    LivePassNode consumer = MakeTestNode("consumer");
    consumer.reads = { "scene" };
    consumer.writes = { "display" };
    valid.AddNode(std::move(consumer));

    std::string validationError;
    record(valid.Validate(validationError), "정상 read/write/optional-modify 기술",
        validationError);

    {
        LivePipelineDesc desc;
        desc.AddNode(MakeTestNode(""));
        expectFailure(std::move(desc), "이름 없는 노드 거부", { "이름 없는" });
    }
    {
        LivePipelineDesc desc;
        LivePassNode node = MakeTestNode("optional-writer");
        node.active = [] { return true; };
        node.writes = { "new-slot" };
        desc.AddNode(std::move(node));
        expectFailure(std::move(desc), "비활성 가능 노드의 새 슬롯 발행 거부",
            { "optional-writer", "new-slot", "modifies" });
    }
    {
        LivePipelineDesc desc;
        LivePassNode node = MakeTestNode("reader");
        node.reads = { "missing" };
        desc.AddNode(std::move(node));
        expectFailure(std::move(desc), "미발행 슬롯 read 거부",
            { "reader", "missing", "읽는다" });
    }
    {
        LivePipelineDesc desc;
        LivePassNode node = MakeTestNode("modifier");
        node.modifies = { "missing" };
        desc.AddNode(std::move(node));
        expectFailure(std::move(desc), "미발행 슬롯 modify 거부",
            { "modifier", "missing", "수정한다" });
    }
    {
        LivePipelineDesc desc;
        LivePassNode first = MakeTestNode("first");
        first.writes = { "color" };
        desc.AddNode(std::move(first));
        LivePassNode second = MakeTestNode("second");
        second.writes = { "color" };
        desc.AddNode(std::move(second));
        expectFailure(std::move(desc), "중복 슬롯 발행 거부",
            { "color", "first", "second", "modifies" });
    }
    {
        LivePipelineDesc desc;
        LivePassNode node{};
        node.name = "no-declare";
        desc.AddNode(std::move(node));
        expectFailure(std::move(desc), "declare 없는 노드 거부",
            { "no-declare", "declare" });
    }

    const std::string dump = valid.Dump();
    record(ContainsAll(dump,
        { "3 nodes", "0. source", "1. optional  [inactive]", "2. consumer",
          "writes: scene", "modifies: scene", "reads: scene", "writes: display" }),
        "덤프 순서·활성 상태·슬롯 연결", "");

    // 조회 문자열의 수명과 저장 키의 수명을 분리해도 핸들 값과 목록 소유권은 같아야 한다.
    const RGHandle first{17, 3, RGResourceKind::Buffer, 41};
    const RGHandle second{23, 9, RGResourceKind::Texture, 43};
    const auto sameHandle = [](RGHandle left, RGHandle right)
    {
        return left.index == right.index && left.version == right.version &&
            left.kind == right.kind && left.epoch == right.epoch;
    };
    const auto sameSlot = [&](const LiveBlackboard& board, std::string_view slot, RGHandle expected)
    {
        const auto handles = board.GetList(slot);
        return sameHandle(board.Get(slot), expected) && handles.size() == 1 &&
            sameHandle(handles.front(), expected);
    };
    LiveBlackboard blackboard;
    {
        const std::string key = "METEOR.Test.OwnedSlotNameAfterSourceDestruction";
        std::string query;
        {
            std::string source = key;
            blackboard.Set(source, first);
            query = source;
            source.assign(source.size(), 'x');
        }
        record(sameSlot(blackboard, key, first) && sameSlot(blackboard, query, first) &&
                sameSlot(blackboard, "METEOR.Test.OwnedSlotNameAfterSourceDestruction", first) &&
                !blackboard.Get("missing-slot").IsValid() && blackboard.GetList("missing-slot").empty(),
            "블랙보드 키 소유·다른 주소의 같은 이름·누락 조회", "");
    }
    {
        const std::string key = "METEOR.Test.BoundedView";
        const std::string padded = "prefix/" + key + "/suffix";
        const std::string_view query{padded.data() + 7, key.size()};
        blackboard.Set(key, first);
        record(sameSlot(blackboard, query, first) && !blackboard.Get(padded).IsValid() &&
                blackboard.GetList(padded).empty() && !blackboard.Get(query.substr(0, query.size() - 1)).IsValid() &&
                blackboard.GetList(query.substr(0, query.size() - 1)).empty(),
            "블랙보드 종료 문자 없는 부분 문자열의 정확한 범위", "");
    }
    {
        const std::string key{"METEOR\0Slot", 11};
        blackboard.Set(key, first);
        blackboard.Set("METEOR", second);
        const std::string differentAddress = key;
        record(sameSlot(blackboard, differentAddress, first) && sameSlot(blackboard, "METEOR", second),
            "블랙보드 중간 NUL을 포함한 이름과 접두 이름 구분", "");
    }
    {
        const std::string_view empty;
        const bool missing = !blackboard.Get(empty).IsValid() && blackboard.GetList(empty).empty();
        blackboard.Set("", first);
        record(missing && sameSlot(blackboard, empty, first) && sameSlot(blackboard, "", first),
            "블랙보드 비어 있는 view의 누락·발행 조회", "");
    }
    {
        const std::string padded = "prefix/METEOR.Test.List/suffix";
        const std::string_view query{padded.data() + 7, 16};
        blackboard.SetList("METEOR.Test.List", {first, second});
        auto copied = blackboard.GetList(query);
        const bool copiedValues = copied.size() == 2 && sameHandle(copied[0], first) && sameHandle(copied[1], second);
        copied.clear();
        const auto preserved = blackboard.GetList(query);
        bool rejected = false;
        try
        {
            (void)blackboard.Get(query);
        }
        catch (const std::runtime_error&)
        {
            rejected = true;
        }
        blackboard.SetList("empty-list", {});
        record(copiedValues && preserved.size() == 2 && sameHandle(preserved[0], first) &&
                sameHandle(preserved[1], second) && rejected && blackboard.GetList("empty-list").empty() &&
                !blackboard.Get("empty-list").IsValid(),
            "블랙보드 목록 값 복사·다중 값의 단일 조회 거부·빈 목록", "");
    }
    {
        blackboard.Reset();
        bool cleared = true;
        const std::array<std::string_view, 7> keys{
            "METEOR.Test.OwnedSlotNameAfterSourceDestruction", "METEOR.Test.BoundedView",
            std::string_view{"METEOR\0Slot", 11}, "METEOR", "", "METEOR.Test.List", "empty-list"};
        for (const auto key : keys)
        {
            if (blackboard.Get(key).IsValid() || !blackboard.GetList(key).empty())
            {
                cleared = false;
            }
        }
        blackboard.Set("METEOR.Test.OwnedSlotNameAfterSourceDestruction", second);
        record(cleared && sameSlot(blackboard, "METEOR.Test.OwnedSlotNameAfterSourceDestruction", second),
            "블랙보드 Reset 뒤 이전 슬롯 제거·새 핸들 발행", "");
    }
    {
        const std::string scalarKey = "METEOR.Test.OwnedScalarAfterQueryDestruction";
        const std::string listKey = "METEOR.Test.OwnedListAfterQueryDestruction";
        std::vector<RGHandle> copiedList;
        bool preserved = true;
        {
            LiveBlackboard board;
            for (uint32_t index = 0; index < 300; ++index)
            {
                board.Set("METEOR.Test.Warmup." + std::to_string(index), first);
            }
            // 큰 표의 버킷을 유지한 채 소수 슬롯만 발행해도 조회 문자열의 수명을 빌려서는 안 된다.
            board.Reset();
            for (uint32_t cycle = 0; cycle < 3; ++cycle)
            {
                const RGHandle current = cycle % 2 == 0 ? first : second;
                const RGHandle other = cycle % 2 == 0 ? second : first;
                board.Set(scalarKey, current);
                board.SetList(listKey, {current, other});
                {
                    std::string scalarQuery = scalarKey;
                    std::string listQuery = listKey;
                    preserved = sameSlot(board, scalarQuery, current) &&
                        sameHandle(board.Get(std::string{scalarKey}), current) && preserved;
                    copiedList = board.GetList(listQuery);
                    const auto temporaryList = board.GetList(std::string{listKey});
                    preserved = temporaryList.size() == 2 && sameHandle(temporaryList[0], current) &&
                        sameHandle(temporaryList[1], other) && preserved;
                    // 같은 버퍼의 내용 변경과 용량을 넘는 재할당을 거친 뒤 조회 원본을 파괴한다.
                    scalarQuery[0] = 'x';
                    listQuery[0] = 'y';
                    scalarQuery.assign(scalarQuery.capacity() + 1, 'x');
                    listQuery.assign(listQuery.capacity() + 1, 'y');
                }
                const auto storedList = board.GetList(listKey);
                preserved = sameSlot(board, scalarKey, current) && storedList.size() == 2 &&
                    sameHandle(storedList[0], current) && sameHandle(storedList[1], other) && preserved;
                board.Reset();
                preserved = !board.Get(scalarKey).IsValid() && board.GetList(scalarKey).empty() &&
                    board.GetList(listKey).empty() && copiedList.size() == 2 &&
                    sameHandle(copiedList[0], current) && sameHandle(copiedList[1], other) && preserved;
            }
        }
        record(preserved && copiedList.size() == 2 && sameHandle(copiedList[0], first) &&
                sameHandle(copiedList[1], second),
            "블랙보드 조회 원본 변경·재할당·파괴 뒤 Reset 반복·보드 파괴 뒤 목록 보존", "");
    }

    const bool complete = passed == total && total == kSelfTestCases;
    outLog += "LivePipelineDesc selftest " + std::to_string(passed) + "/"
        + std::to_string(total) + (complete ? " 통과\n" : " 실패\n");
    return complete;
}

