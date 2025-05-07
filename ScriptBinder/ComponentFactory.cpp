#include "ComponentFactory.h"
#include "GameObject.h"
#include "RenderableComponents.h"
#include "LightComponent.h"
#include "CameraComponent.h"
#include "DataSystem.h"
#include "AnimationController.h"
#include "Model.h"
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
					ConditionParameter aniParam;
					Meta::Deserialize(&aniParam, param);
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
					//animator->m_animationController = animationController;
					if (layer["StateVec"])
					{
						auto& StatesNode = layer["StateVec"];
						for (auto& state : StatesNode)
						{
							std::shared_ptr<AnimationState> sharedState = std::make_shared<AnimationState>();
							Meta::Deserialize(sharedState.get(), state);
							animationController->StateVec.push_back(sharedState);
							animationController->States.insert(std::make_pair(sharedState->Name, animationController->StateVec.size() - 1));
							sharedState->m_ownerController = animationController;
							sharedState->SetBehaviour(sharedState->Name);
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
										}
									}
								}
							}
						}
					}
					if (layer["m_curState"])
					{
						auto& curNode = layer["m_curState"];
						std::string name = curNode["Name"].as<std::string>();
						animationController->m_curState = animationController->StateVec[animationController->States[name]].get();
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
		else
		{
            Meta::Deserialize(component, itNode);
			component->SetOwner(obj);
		}
    }
}
