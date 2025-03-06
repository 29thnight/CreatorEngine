#pragma once
#include "Utility_Framework/IObject.h"
#include <ranges>
#include "Component.h"
#include "ComponentFactory.h"
#include "ObjectTypeMeta.h"

class Object : public IObject
{
public:
	Object() : _name("Object"), _parent(nullptr) {};
	Object(const std::string& name) : _name(name), _parent(nullptr) {};
	virtual ~Object();
	virtual void Initialize() override;
	virtual void FixedUpdate(float fixedTick) override;
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override;
	virtual void Destroy() override;
	virtual void Serialize(_inout json& out) override;
	virtual void DeSerialize(_in json& in) override;

	void DestroyMark() { _isDestroyed = true; }
	bool IsDestroyedMark() const { return _isDestroyed; }

	void DestroyComponentStage();

	template<typename T>
	T* AddComponent()
	{
		T* component = new T();
		_components.push_back(component);
		component->SetOwner(this);
		_componentIds[component->GetId()] = _components.size();

		std::ranges::sort(_components, [&](Component* a, Component* b)
		{
			return a->ID() < b->ID();
		});

		foreach(iota(0, static_cast<int>(_components.size())), [&](int i)
		{
			_componentIds[_components[i]->GetId()] = i;
		});

		component->Initialize();

		return component;
	}

	template<typename T, typename... Args>
	T* AddComponent(Args&&... args)
	{
		T* component = new T(std::forward<Args>(args)...);
		_components.push_back(component);
		component->SetOwner(this);
		_componentIds[component->ID()] = _components.size();
		std::ranges::sort(_components, [&](Component* a, Component* b)
		{
			return a->ID() < b->ID();
		});

		foreach(iota(0, static_cast<int>(_components.size())), [&](int i)
		{
			_componentIds[_components[i]->ID()] = i;
		});

		component->Initialize();

		return component;
	}

	template<typename T>
	T* GetComponent(uint32 id)
	{
		auto it = _componentIds.find(id); 
		if (it == _componentIds.end())
			return nullptr;
		return static_cast<T*>(&_components[it->second]);
	}

    template<typename T>
    T* GetComponent()
    {
        for (auto& component : _components)
        {
            if (T* castedComponent = dynamic_cast<T*>(component))
                return castedComponent;
        }
        return nullptr;
    }

	template<typename T>
	std::vector<T*> GetComponents() {
		std::vector<T*> comps;
		for (auto& component : _components)
		{
			if (T* castedComponent = dynamic_cast<T*>(component))
				comps.push_back(castedComponent);
		}
		return comps;
	}

	template<typename T>
	void RemoveComponent(T* component)
	{
		component->SetDestroyMark();
	}
	
	void DeserializeAddcomponent(json& in)
	{
		for (json& component : in)
		{
			std::string type = component["Type"].get<std::string>();
			auto newComponent = ComponentFactory::GetInstance()->CreateComponent(type);
			newComponent->DeSerialize(component);
			_components.push_back(newComponent);
			newComponent->SetOwner(this);
			_componentIds[newComponent->ID()] = _components.size();

			std::ranges::sort(_components, [&](Component* a, Component* b)
			{
				return a < b;
			});

			foreach(iota(0, static_cast<int>(_components.size())), [&](int i)
			{
				_componentIds[_components[i]->ID()] = i;
			});

			newComponent->Initialize();
		}
	}

	void SetParent(Object* parent) { _parent = parent; _parent->_children.push_back(this); }
	Object* GetParent() const { return _parent; }
	std::string GetName() const { return _name; }

	//기본 변위 함수
	void SetScale(const Mathf::Vector3& scale) { _scale = scale; }
	void SetPosition(const Mathf::Vector3& position) { _position = position; }
	void SetRotation(const Mathf::Vector3& rotation) { _rotation = rotation; }
	void translate(const Mathf::Vector3& translation) { _position += translation; }
	void rotate(const Mathf::Vector3& rotation) { _rotation += rotation; }
	void scale(const Mathf::Vector3& scale) { _scale += scale; }

	Mathf::Vector3 GetLocalPosition() const { return _position; }
	Mathf::Vector3 GetLocalRotation() const { return _rotation; }
	Mathf::Vector3 GetLocalScale() const { return _scale; }

	Mathf::Matrix GetWorldMatrix() const 
	{
		Mathf::Matrix scale = Mathf::Matrix::CreateScale(_scale);
		Mathf::Quaternion newRotation = Mathf::Quaternion::CreateFromYawPitchRoll(
			XMConvertToRadians(_rotation.y),
			XMConvertToRadians(_rotation.x),
			XMConvertToRadians(_rotation.z)
		);
		Mathf::Matrix rotation = Mathf::Matrix::CreateFromQuaternion(newRotation);
		Mathf::Vector3 convertedPosition = _position * 10.f;
		Mathf::Matrix position = Mathf::Matrix::CreateTranslation(convertedPosition);
		return scale * rotation * position;
	};

	// ITriggerNotify을(를) 통해 상속됨
	virtual void OnTriggerEnter(ICollider* other) {};
	virtual void OnTriggerStay(ICollider* other) {};
	virtual void OnTriggerExit(ICollider* other) {};

	// ICollisionNotify을(를) 통해 상속됨
	virtual void OnCollisionEnter(ICollider* other) {};
	virtual void OnCollisionStay(ICollider* other) {};
	virtual void OnCollisionExit(ICollider* other) {};

	virtual bool IsIgnoreInteract() { return true; }
	virtual void InteractEnter(Object* thisPlayer, int playerID) {}
	virtual void Interact(Object* thisPlayer, int playerID, float tick) {}
	virtual void InteractExit(Object* thisPlayer, int playerID) {}
	virtual void BornItem() {}
	virtual void EditorContext() {}
	virtual std::string_view GetType() override;

protected:
	friend class World;
	Object* _parent; //부모 오브젝트

	std::string _name;	//오브젝트 이름

	std::unordered_map<uint32, size_t> _componentIds;	//컴포넌트 아이디들
	std::vector<Component*> _components;	//컴포넌트들
	std::vector<Object*> _children;	//자식 오브젝트들

	//오브젝트 기본 변위값
	Mathf::Vector3 _scale{ 1.f, 1.f, 1.f };
	Mathf::Vector3 _position;
	Mathf::Vector3 _rotation;

	bool _isDestroyed = false;


protected:
	struct playerTime {
		int playerID;
		union Value {
			int count;
			float time;
		};
		Value value;
		bool use = false;	// 해당 플레이어가 사용중인 상태.
	};
	playerTime players[2];

	void HighLightOn();
	void HighLightOff();
};

template<>
struct MetaType<Object>
{
	static constexpr std::string_view type{ "Object" };
};
