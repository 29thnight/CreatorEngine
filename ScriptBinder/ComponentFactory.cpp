#include "ComponentFactory.h"
#include "GameObject.h"
#include "RenderableComponents.h"
#include "LightComponent.h"
#include "CameraComponent.h"
#include "DataSystem.h"
#include "AnimationController.h"
#include "CharacterControllerComponent.h"
#include "Model.h"
#include "NodeEditor.h"
void ComponentFactory::Initialize()
{
   auto& registerMap = Meta::MetaDataRegistry->map;

   for (const auto& [name, type] : registerMap)
   {
	   size_t pos = name.find("Component");
	   if (pos != std::string::npos)
	   {
		   m_componentTypes[name] = &type;
	   }
	   pos = name.find("Renderer");
	   if (pos != std::string::npos)
	   {
		   m_componentTypes[name] = &type;
	   }
	   pos = name.find("Animator");
	   if (pos != std::string::npos)
	   {
		   m_componentTypes[name] = &type;
	   }
   }
}

void ComponentFactory::LoadComponent(GameObject* obj, const MetaYml::detail::iterator_value& itNode)
{
	if (itNode["ModuleBehavior"])
	{
		std::string scriptName = itNode["m_name"].as<std::string>();
		auto scriptComponent = obj->AddScriptComponent(scriptName);
		return;
	}

    const Meta::Type* componentType = Meta::ExtractTypeFromYAML(itNode);
    if (nullptr == componentType)
    {
        return;
    }

    auto component = obj->AddComponent((*componentType)).get();
    if (component)
    {
        using namespace TypeTrait;
        if (componentType->typeID == type_guid(MeshRenderer))
        {
            auto meshRenderer = static_cast<MeshRenderer*>(component);
            Model* model = nullptr;
            Meta::Deserialize(meshRenderer, itNode);
            if (itNode["m_Material"])
            {
                auto materialNode = itNode["m_Material"];
                FileGuid guid = materialNode["m_fileGuid"].as<std::string>();
                model = DataSystems->LoadModelGUID(guid);
            }
            MetaYml::Node getMeshNode = itNode["m_Mesh"];
            if (model && getMeshNode)
            {
                meshRenderer->m_Material = model->GetMaterial(getMeshNode["m_materialIndex"].as<int>());
                meshRenderer->m_Mesh = model->GetMesh(getMeshNode["m_name"].as<std::string>());
            }
			meshRenderer->SetOwner(obj);
            meshRenderer->SetEnabled(true);
        }
		else if (componentType->typeID == type_guid(Animator))
		{
			auto animator = static_cast<Animator*>(component);
			Model* model = nullptr;

			if (itNode["m_Motion"])
			{
				FileGuid guid = itNode["m_Motion"].as<std::string>();
				animator->m_Skeleton = DataSystems->LoadModelGUID(guid)->m_Skeleton;
			}

			if (itNode["Parameters"])
			{
				auto& paramNode = itNode["Parameters"];

				for (auto& param : paramNode)
				{
					ConditionParameter* aniParam = new ConditionParameter();
					Meta::Deserialize(aniParam, param);
					animator->Parameters.push_back(aniParam);
				}
			}

			if(itNode["m_animationControllers"])
			{
				
				auto& animationControllerNode = itNode["m_animationControllers"];

				for (auto& layer : animationControllerNode)
				{

					AnimationController* animationController = new AnimationController();
					Meta::Deserialize(animationController, layer);
					animationController->m_owner = animator;
					animationController->m_nodeEditor = new NodeEditor();

					if (animationController->useMask == true)
					{
						if (layer["m_avatarMask"])
						{
							auto& MaskNode = layer["m_avatarMask"];
							AvatarMask avatarMask;
							Meta::Deserialize(&avatarMask, MaskNode);
							animationController->ReCreateMask(&avatarMask);
						}
					}
					if (layer["StateVec"])
					{
						auto& StatesNode = layer["StateVec"];
						for (auto& state : StatesNode)
						{
							std::shared_ptr<AnimationState> sharedState = std::make_shared<AnimationState>();
							Meta::Deserialize(sharedState.get(), state);
							animationController->StateVec.push_back(sharedState);
							animationController->States.insert(std::make_pair(sharedState->m_name, animationController->StateVec.size() - 1));
							sharedState->m_ownerController = animationController;
							sharedState->SetBehaviour(sharedState->m_name);
							if (state["Transitions"])
							{
								auto& transitionNode = state["Transitions"];
								
								for (auto& transition : transitionNode)
								{
									std::shared_ptr<AniTransition> sharedTransition = std::make_shared<AniTransition>();
									Meta::Deserialize(sharedTransition.get(), transition);
									sharedState->Transitions.push_back(sharedTransition);
									sharedTransition->m_ownerController = animationController; 
								
									if (transition["conditions"])
									{
										auto& conditionNode = transition["conditions"];
										for (auto& condition : conditionNode)
										{
											TransCondition newcondition;
											Meta::Deserialize(&newcondition, condition);
											newcondition.m_ownerController = animationController;
											sharedTransition->conditions.push_back(newcondition);
											newcondition.SetValue(newcondition.valueName);
				
										}
									}
								}
							}
						}
					}
					if (layer["m_curState"])
					{
						auto& curNode = layer["m_curState"];
						std::string name = curNode["m_name"].as<std::string>();
						animationController->m_curState = animationController->FindState(name);
					}

					for (auto& state : animationController->StateVec)
					{
						for (auto& transition : state->Transitions)
						{
							transition->SetCurState(transition->curStateName);
							transition->SetNextState(transition->nextStateName);
						}
					}

						
					animator->m_animationControllers.push_back(animationController);
				}
			}
		}
		else if (componentType->typeID == type_guid(LightComponent))
		{
			auto lightComponent = static_cast<LightComponent*>(component);
            Meta::Deserialize(lightComponent, itNode);
			lightComponent->SetOwner(obj);
			lightComponent->SetEnabled(true);
		}
		else if (componentType->typeID == type_guid(CameraComponent))
		{
			auto cameraComponent = static_cast<CameraComponent*>(component);
			Meta::Deserialize(cameraComponent, itNode);
			cameraComponent->SetOwner(obj);
			cameraComponent->SetEnabled(true);
		}
		else if (componentType->typeID == type_guid(SpriteRenderer))
		{
			auto spriteRenderer = static_cast<SpriteRenderer*>(component);
			Texture* texture = nullptr;
			if (itNode["m_Sprite"])
			{
				auto spriteNode = itNode["m_Sprite"];
				FileGuid guid = spriteNode["m_fileGuid"].as<std::string>();
			}
            Meta::Deserialize(spriteRenderer, itNode);
			spriteRenderer->SetOwner(obj);
			spriteRenderer->SetEnabled(true);
		}
		else if (componentType->typeID == type_guid(RigidBodyComponent))
		{
			auto rigidBody = static_cast<RigidBodyComponent*>(component);
			Meta::Deserialize(rigidBody, itNode);
			rigidBody->SetOwner(obj);
			}
		else if (componentType->typeID == type_guid(CharacterControllerComponent))
		{
			auto characterController = static_cast<CharacterControllerComponent*>(component);
			Meta::Deserialize(characterController, itNode);
			characterController->SetOwner(obj);
		}
		else
		{
            Meta::Deserialize(component, itNode);
			component->SetOwner(obj);
		}
    }
}
