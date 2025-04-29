#include "ComponentFactory.h"
#include "GameObject.h"
#include "RenderableComponents.h"
#include "LightComponent.h"
#include "CameraComponent.h"
#include "DataSystem.h"
#include "aniFSM.h"

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
        if (componentType->typeID == GUIDCreator::GetTypeID<MeshRenderer>())
        {
            auto meshRenderer = static_cast<MeshRenderer*>(component);
            Model* model = nullptr;
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
            Meta::Deserialize(meshRenderer, itNode);
            meshRenderer->SetEnabled(true);
        }
		else if (componentType->typeID == GUIDCreator::GetTypeID<Animator>())
		{
			auto animator = static_cast<Animator*>(component);
			Model* model = nullptr;

			if (itNode["m_Motion"])
			{
				FileGuid guid = itNode["m_Motion"].as<std::string>();
				animator->m_Skeleton = DataSystems->LoadModelGUID(guid)->m_Skeleton;
			}
		}
		else if (componentType->typeID == GUIDCreator::GetTypeID<LightComponent>())
		{
			auto lightComponent = static_cast<LightComponent*>(component);
            Meta::Deserialize(lightComponent, itNode);
			lightComponent->SetEnabled(true);
		}
		//else if (componentType->typeID == GUIDCreator::GetTypeID<CameraComponent>())
		//{
		//	auto cameraComponent = static_cast<CameraComponent*>(component);
		//	Deserialize(cameraComponent, itNode);
		//	cameraComponent->SetEnabled(true);
		//}
		else if (componentType->typeID == GUIDCreator::GetTypeID<SpriteRenderer>())
		{
			//auto spriteRenderer = static_cast<SpriteRenderer*>(component);
			//Texture* texture = nullptr;
			//if (itNode["m_Sprite"])
			//{
			//	auto spriteNode = itNode["m_Sprite"];
			//	FileGuid guid = spriteNode["m_fileGuid"].as<std::string>();
			//}
   //         Meta::Deserialize(spriteRenderer, itNode);
			//spriteRenderer->SetEnabled(true);
		}
		else if (componentType->typeID == GUIDCreator::GetTypeID<aniFSM>())
		{
			auto aniFSMcomponent = static_cast<aniFSM*>(component);
			if (itNode["StateVec"])
			{
				auto& FSMNode = itNode["StateVec"];
				for(auto& fsm : FSMNode)
				{
					std::shared_ptr<aniState> sharedState = std::make_shared<aniState>();
					Meta::Deserialize(sharedState.get(), fsm);
					aniFSMcomponent->StateVec.push_back(sharedState);
					aniFSMcomponent->States.insert(std::make_pair(sharedState->Name, aniFSMcomponent->StateVec.size()-1));
					sharedState->Owner = aniFSMcomponent;
					sharedState->SetBehaviour(sharedState->Name);
					if (fsm["Transitions"])
					{
						auto& transitionNode = fsm["Transitions"];
						for (auto& transition : transitionNode)
						{
							std::shared_ptr<AniTransition> sharedTransition = std::make_shared<AniTransition>();
							Meta::Deserialize(sharedTransition.get(), transition);
							sharedState->Transitions.push_back(sharedTransition);
							sharedTransition->owner = aniFSMcomponent;				
							if (transition["conditions"])
							{
								auto& conditionNode = transition["conditions"];
								for (auto& condition : conditionNode)
								{
									TransCondition newcondition;
									Meta::Deserialize(&newcondition, condition);
									newcondition.ownerFSM = aniFSMcomponent;
									sharedTransition->conditions.push_back(newcondition);
								}
							}
						}
					}
				}
			}
			if (itNode["Parameters"])
			{
				auto& paramNode = itNode["Parameters"];

				for (auto& param : paramNode)
				{
					aniParameter aniParam;
					Meta::Deserialize(&aniParam, param);
					aniFSMcomponent->Parameters.push_back(aniParam);
				}
			}
			if (itNode["CurState"])
			{
				auto& curNode = itNode["CurState"];
				std::string name = curNode["Name"].as<std::string>();
				aniFSMcomponent->CurState = aniFSMcomponent->StateVec[aniFSMcomponent->States[name]].get();
			}
			if (itNode["animator"])
			{
				auto ani = aniFSMcomponent->GetOwner()->GetComponent<Animator>();
				aniFSMcomponent->SetAnimator(ani);
			}
			Meta::Deserialize(aniFSMcomponent, itNode);
		}
		else
		{
            Meta::Deserialize(component, itNode);
		}
    }
}
