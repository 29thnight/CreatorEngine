#include "BehaviorTreeComponent.h"

ImVec2 BehaviorTreeComponent::GetNodePosition(const BT::BTNode::NodePtr& node) const
{
	auto it = _nodePositions.find(node.get());
	if (it != _nodePositions.end()) {
		return it->second; // Return the stored position if found
	}
	return ImVec2(0, 0); // Default position if not found
}

void BehaviorTreeComponent::SetNodePosition(const BT::BTNode::NodePtr& node, const ImVec2& pos)
{
	_nodePositions[node.get()] = pos; // Store the position for the node
}
