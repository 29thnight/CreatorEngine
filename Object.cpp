#include "Object.h"
#include "Utility_Framework/Core.Memory.hpp"
#include "../RenderEngine/ImGuiRegister.h"

Object::~Object()
{
	Destroy();
}

void Object::Initialize()
{
	//todo: Initialize
	//_componentIds.reserve(10); //기본값 10개 변경가능
	_components.reserve(10);
}

void Object::FixedUpdate(float fixedTick)
{
	for (auto& compo : _components)
	{
		if(compo->IsDestroyMark())
			continue;

		compo->FixedUpdate(fixedTick);
	}

	for (auto& child : _children)
	{
		child->FixedUpdate(fixedTick);
	}
}

void Object::Update(float tick)
{
	for (auto& compo : _components) 
	{
		if (compo->IsDestroyMark())
			continue;

		compo->Update(tick);
	}

	std::ranges::remove_if(_components, [&](Component* comp) 
	{ 
		return comp->IsDestroyMark(); 
	});

	for (auto& child : _children) 
	{
		child->Update(tick);
	}
}

void Object::LateUpdate(float tick)
{
	for (auto& compo : _components)
	{
		if (compo->IsDestroyMark())
			continue;

		compo->LateUpdate(tick);
	}

	for (auto& child : _children)
	{
		child->LateUpdate(tick);
	}
}

void Object::Destroy()
{
	for (auto& child : _children) 
	{
		child->Destroy();
	}

	for (auto& component : _components)
	{
		Memory::SafeDelete(component);
	}

	_components.clear();
	_children.clear();
}

void Object::Serialize(_inout json& out)
{
	json data;

	data["components"] = json::array();
	data["componentCount"] = _components.size();
	data["scale"] = { _scale.x, _scale.y, _scale.z };
	data["rotation"] = { _rotation.x, _rotation.y, _rotation.z };
	data["position"] = { _position.x, _position.y, _position.z };
	data["name"] = _name;
	data["type"] = MetaType<Object>::type;
	for (auto& component : _components)
	{
		component->Serialize(data);
	}

	out["World"]["objects"].push_back(data);
}

void Object::DeSerialize(_in json& in)
{
	_position = Mathf::jsonToVector3(in["position"]);
	_rotation = Mathf::jsonToVector3(in["rotation"]);
	_scale = Mathf::jsonToVector3(in["scale"]);

}

void Object::DestroyComponentStage()
{
	std::vector<uint32> indecesToRemove;

	for (auto& [key, value] : _componentIds)
	{
		if (_components[value]->IsDestroyMark())
		{
			indecesToRemove.push_back(value);
		}
	}

	std::ranges::sort(indecesToRemove, std::greater());

	for (uint32 index : indecesToRemove)
	{
		uint32 lastIndex{};

		if (0 == _components.size())
		{
			return;
		}
		else
		{
			lastIndex = (uint32)_components.size() - 1;
		}

		if (index != lastIndex)
		{
			std::swap(_components[index], _components[lastIndex]);

			for (auto& [key, value] : _componentIds)
			{
				if (value == lastIndex)
				{
					value = index;
					break;
				}
			}
		}

		Memory::SafeDelete(_components[lastIndex]);
		_components.pop_back();
	}

	std::erase_if(_componentIds, [&](const auto& pair)
	{
		return std::ranges::find(indecesToRemove, pair.second) != indecesToRemove.end();
	});

	for (uint32 i = 0; i < _components.size(); i++)
	{
		_componentIds[_components[i]->ID()] = i;
	}
}

std::string_view Object::GetType()
{
	return MetaType<Object>::type;
}

void Object::HighLightOn()
{
	//auto render = GetComponents<RenderComponent>();
	//for (auto r : render) {
	//	if (r) {
	//		auto model = r->GetInstancedModel();
	//		if (model) {
	//			model->outline.color = { 1.f, 1.f, 1.f, 1.f };
	//			auto scale = r->GetInstancedModel()->scale;
	//			float size = scale.m128_f32[0] + scale.m128_f32[1] + scale.m128_f32[2];
	//			size *= 0.33f;
	//			model->outline.oulineThickness = 0.1f / size;
	//		}
	//	}
	//}
}

void Object::HighLightOff()
{
	/*auto render = GetComponents<RenderComponent>();
	for (auto r : render) {
		if (r) {
			auto model = r->GetInstancedModel();
			if (model) {
				model->outline.color = { 0.f, 0.f, 0.f, 1.f };
				auto scale = r->GetInstancedModel()->scale;
				float size = scale.m128_f32[0] + scale.m128_f32[1] + scale.m128_f32[2];
				size *= 0.33f;
				model->outline.oulineThickness = 0.008f / size;
			}
		}
	}*/
}


