#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "ClrHost.h"
#include "BTBuildGraph.h"
#include "Blackboard.h"
#include <vector>

// 저작 그래프를 경계 전달 형태로 편다 (PHASE 9-8 · B3).
//
// ── 왜 별도 단계인가 ──
//
// BTBuildGraph는 unordered_map<HashedGuid, BTBuildNode*>와 std::string, std::vector를
// 품고 있어 그대로는 경계를 넘길 수 없다. 그렇다고 노드마다 넘기면 크로싱이 노드 수만큼
// 생기는데, 그것이 정확히 이 재설계가 없애려던 것이다(BT의 틱은 동기 재귀라 배치로
// 묶을 수 없다 — 설계 문서 §1).
//
// 그래서 저작 데이터의 형식은 그대로 두고, 넘기는 순간에만 평평한 POD 배열로 편다.
// 기존 BT 에셋이 수정 없이 열리는 것이 이 재설계의 완료 기준이기도 하다.
namespace BTFlatten
{
    /// 그래프를 노드 배열로 편다. 실패하면 빈 벡터.
    ///
    /// 자식 순서는 저작 그래프의 Children 순서를 그대로 childOrder에 싣는다 —
    /// Sequence·Selector는 자식 순서가 곧 실행 순서라, 여기서 흔들리면 같은 그래프가
    /// 다른 행동을 한다.
    inline std::vector<ClrHost::ScriptBTNodeDesc> Flatten(const BTBuildGraph& graph)
    {
        std::vector<ClrHost::ScriptBTNodeDesc> out;
        out.reserve(graph.NodeList.size());

        // 부모 → 자식 순서·가중치를 먼저 훑는다.
        //
        // 자식 쪽에는 "내가 부모의 몇 번째인가"가 없고 부모의 Children에만 있다.
        // 그 정보를 자식 항목으로 옮겨 실어야 관리 측이 순서를 복원할 수 있다.
        std::unordered_map<size_t, int>   orderById;
        std::unordered_map<size_t, float> weightById;

        for (const BTBuildNode& parent : graph.NodeList)
        {
            for (size_t i = 0; i < parent.Children.size(); ++i)
            {
                const size_t childId = parent.Children[i].m_ID_Data;
                orderById[childId] = static_cast<int>(i);

                // 가중치는 WeightedSelector에서만 의미가 있고, 저작 쪽에서 개수가
                // 자식보다 적을 수 있다(나중에 자식만 추가한 경우). 없으면 0.
                weightById[childId] = (i < parent.ChildWeights.size())
                    ? parent.ChildWeights[i] : 0.0f;
            }
        }

        auto copyName = [](char* dst, size_t cap, const std::string& src)
        {
            const size_t n = (src.size() < cap - 1) ? src.size() : cap - 1;
            std::memcpy(dst, src.data(), n);
            dst[n] = '\0';
        };

        for (const BTBuildNode& node : graph.NodeList)
        {
            ClrHost::ScriptBTNodeDesc desc{};
            desc.id        = node.ID.m_ID_Data;
            desc.parentId  = node.ParentID.m_ID_Data;
            desc.type      = static_cast<int32_t>(node.Type);
            desc.policy    = static_cast<int32_t>(node.Policy);
            desc.isRoot    = node.IsRoot ? 1 : 0;
            desc.hasScript = node.HasScript ? 1 : 0;

            if (auto it = orderById.find(desc.id); it != orderById.end())
                desc.childOrder = it->second;
            if (auto it = weightById.find(desc.id); it != weightById.end())
                desc.weight = it->second;

            copyName(desc.name, ClrHost::kBTNameCapacity, node.Name);
            copyName(desc.scriptName, ClrHost::kBTNameCapacity, node.ScriptName);

            out.push_back(desc);
        }

        return out;
    }

    /// 저작 블랙보드를 항목 배열로 편다.
    ///
    /// 관리 측 트리는 자기 블랙보드를 쓴다. 저작 값을 실어 보내지 않으면 노드가 읽는
    /// 값이 전부 기본값이 되고, 그 차이는 크래시가 아니라 "AI가 좀 이상하다"로만
    /// 나타나 원인을 짚기 어렵다.
    ///
    /// 네이티브 BlackBoard는 이제 저작·직렬화 전용이다 — BTBuildGraph와 같은 자리다.
    inline std::vector<ClrHost::ScriptBBEntry> FlattenBlackBoard(const BlackBoard& board)
    {
        std::vector<ClrHost::ScriptBBEntry> out;

        auto copyText = [](char* dst, size_t cap, const std::string& src)
        {
            const size_t n = (src.size() < cap - 1) ? src.size() : cap - 1;
            std::memcpy(dst, src.data(), n);
            dst[n] = '\0';
        };

        for (const auto& [key, value] : board.GetValues())
        {
            ClrHost::ScriptBBEntry entry{};
            entry.type = static_cast<int32_t>(value.Type);
            copyText(entry.key, ClrHost::kBBKeyCapacity, key);

            switch (value.Type)
            {
            case BlackBoardType::Bool:   entry.boolValue = value.BoolValue ? 1 : 0; break;
            case BlackBoardType::Int:    entry.intValue = value.IntValue; break;
            case BlackBoardType::Float:  entry.floatValue = value.FloatValue; break;
            case BlackBoardType::Vector2: entry.x = value.Vec2Value.x; entry.y = value.Vec2Value.y; break;
            case BlackBoardType::Vector3:
                entry.x = value.Vec3Value.x; entry.y = value.Vec3Value.y; entry.z = value.Vec3Value.z;
                break;
            case BlackBoardType::Vector4:
                entry.x = value.Vec4Value.x; entry.y = value.Vec4Value.y;
                entry.z = value.Vec4Value.z; entry.w = value.Vec4Value.w;
                break;
            case BlackBoardType::String:
            case BlackBoardType::Entity:
            case BlackBoardType::Transform:
                copyText(entry.stringValue, ClrHost::kBBStringCapacity, value.StringValue);
                break;
            default:
                break;
            }

            out.push_back(entry);
        }

        return out;
    }
}
#endif // !DYNAMICCPP_EXPORTS
