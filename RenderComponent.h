#pragma once
#include "Component.h"
#include "RenderEngine/Scene.h"
#include "ObjectTypeMeta.h"

struct ModelSerializeData
{
	Mathf::Vector3 scale;
	Mathf::Vector3 rotation;
	Mathf::Vector3 position;
	Mathf::Color4 outlineColor;
	float outlineThickness;
};

//class RenderComponent final : public Component
//{
//public:
////	RenderComponent(Scene* scene);
////	virtual ~RenderComponent();
////	virtual void Initialize() override final {};
////	virtual void FixedUpdate(float fixedTick) override final {};
////	virtual void Update(float tick) override;
////	virtual void LateUpdate(float tick) override final {};
////	virtual void EditorContext() override;
////	virtual void Serialize(_inout json& out) override final;
////	virtual void DeSerialize(_in json& in) override final;
////	void SetInstancedModel(InstancedModel* instancedModel)
////	{ 
////		_instancedModel = instancedModel; 
////	}
////
////	void SetVisiable(bool isVisiable)
////	{
////		_isVisiable = isVisiable;
////		if(_instancedModel)
////			_instancedModel->isVisiable = isVisiable;
////	}
////	void SetSubVisible(bool isVisible) {
////		_isSubVisiable = isVisible;
////		if (_subInstancedModel)
////			_subInstancedModel->isVisiable = isVisible;
////	}
////	void SetAxeVisible(bool isVisible) {
////		_isAxeVisiable = isVisible;
////		if (_axeInstancedModel)
////			_axeInstancedModel->isVisiable = isVisible;
////	}
////
////	InstancedModel* GetInstancedModel() const
////	{ 
////		return _instancedModel!=nullptr ? _instancedModel : nullptr;
////	}
////
////	InstancedModel* GetSubInstancedModel() const
////	{
////		return _subInstancedModel != nullptr ? _subInstancedModel : nullptr;;
////	}
////
////	InstancedModel* GetAxeInstancedModel() const
////	{
////		return _axeInstancedModel != nullptr ? _axeInstancedModel : nullptr;;
////	}
////
////	uint32 ID() override final
////	{
////		return _ID;
////	}
////
////private:
////	static constexpr uint32 _ID{ UINT32_MAX };
////	Scene* _targetScene{};
////	uint32 _instancedID{ UINT32_MAX };
////	InstancedModel* _instancedModel{};
////	//플레이어 전용
////	InstancedModel* _subInstancedModel{};
////	InstancedModel* _axeInstancedModel{};
////
////	std::string _instancedName;
////	//플레이어 전용
////	std::string _subInstancedName;
////	std::string _axeInstancedName;
////
////	std::string _modelName;
////	//플레이어 전용
////	std::string _subModelName;
////	std::string _axeModelName;
////
////	bool _isVisiable{ true };
////	//플레이어 전용
////	bool _isSubVisiable{ false };
////	bool _isAxeVisiable{ false };
////
////	bool _isDeSerialize{ true };
////	float progress = 0.0f;
////	ModelSerializeData _notYetSerializeData;
//};
//
//template<>
//struct MetaType<RenderComponent>
//{
//	static constexpr std::string_view type{ "RenderComponent" };
//};