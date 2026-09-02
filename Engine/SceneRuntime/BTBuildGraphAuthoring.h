#pragma once

#include "BTBuildGraph.h"
#include "AuthoringReadNode.h"

// BTBuildGraph.h는 아직 CP949라 공용 멤버 시그니처를 안전하게 바꿀 수 없다.
// 런타임과 에디터의 읽기 경로가 로직을 복제하지 않도록 UTF-8 경계에 둔 임시 브리지다.
namespace BTBuildGraphAuthoring
{
	inline void DeserializeSingleNode(BTBuildGraph& graph,
		const Authoring::ReadNode& node)
	{
		BTBuildNode out;
		Meta::Deserialize(&out, node);
		graph.NodeList.push_back(out);
		graph.Nodes[out.ID] = &graph.NodeList.back();
		if (out.IsRoot) graph.SelectedNode = &graph.NodeList.back();
	}
}
