#include "AScriptComponent.h"
#include "AScriptBehaviourWrapper.h"

AScriptComponent::AScriptComponent()
{
	m_name = "AScriptComponent"; 
	m_typeID = TypeTrait::GUIDCreator::GetTypeID<AScriptComponent>();
}

void AScriptComponent::Start()
{
	if (m_scriptBehaviourWrapper)
	{
		m_scriptBehaviourWrapper->OnStart();
	}
}

void AScriptComponent::Update(float deltaTime)
{
	if (m_scriptBehaviourWrapper)
	{
		m_scriptBehaviourWrapper->OnUpdate(deltaTime);
	}
}

