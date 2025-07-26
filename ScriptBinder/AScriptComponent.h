#pragma once
#include "Component.h"
#include "IRegistableEvent.h"
#include "AScriptComponent.generated.h"

class AScriptBehaviourWrapper;
class AScriptComponent : public Component, public RegistableEvent<AScriptComponent>
{
public:
   ReflectAScriptComponent
	[[Serializable(Inheritance:Component)]]
	AScriptComponent();
   virtual ~AScriptComponent() = default;

	void SetScriptBehaviourWrapper(AScriptBehaviourWrapper* wrapper) { m_scriptBehaviourWrapper = wrapper; }
	AScriptBehaviourWrapper* GetScriptBehaviourWrapper() const { return m_scriptBehaviourWrapper; }

	void Start() override;

	void Update(float deltaTime) override;

	[[Property]]
	FileGuid m_scriptGuid; // 스크립트 파일의 GUID
	[[Property]]
	std::string m_scriptName; // 스크립트 이름

private:
	AScriptBehaviourWrapper* m_scriptBehaviourWrapper = nullptr;
};
