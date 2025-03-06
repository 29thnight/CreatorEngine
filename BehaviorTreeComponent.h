#pragma once
#include "Component.h"
#include "BehaviorTree.h"
#include "ObjectTypeMeta.h"
//��� BT�� ���� ����ȭ�� �Ͼ�� ������ ������ȭ �Ұ�.
class BaseBehaviorTree;
class BehaviorTreeComponent : public Component
{
public:
	BehaviorTreeComponent(std::unique_ptr<BaseBehaviorTree> tree) : _behaviorTree(std::move(tree))
	{
		_behaviorTree->buildTree();
	}
	// Component��(��) ���� ��ӵ�
	void Initialize() override;
	void FixedUpdate(float fixedTick) override;
	void Update(float tick) override;
	void LateUpdate(float tick) override;
	void EditorContext() override;
	virtual void Serialize(_inout json& out) override final;
	virtual void DeSerialize(_in json& in) override final;
	template<typename T, typename U>
	void updateState(const U& state) 
	{
		if (auto tree = dynamic_cast<T*>(_behaviorTree.get()))
		{
			tree->updateState(state);
		}
	}
	uint32 ID() override { return _ID; }
	bool _isSuccess{ false };
private:
	static constexpr uint32 _ID{ 5004 };
	std::unique_ptr<BaseBehaviorTree> _behaviorTree;
};

template<>
struct MetaType<BehaviorTreeComponent>
{
	static constexpr std::string_view type{ "BehaviorTreeComponent" };
};