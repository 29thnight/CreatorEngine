#include "ComponentFactory.h"
#include "GameObject.h"
#include "RenderableComponents.h"
#include "LightComponent.h"
#include "CameraComponent.h"
#include "DataSystem.h"
#include "AnimationController.h"
#include "BoxColliderComponent.h"
#include "CharacterControllerComponent.h"
#include "Terrain.h"
#include "Model.h"
#include "NodeEditor.h"
#include "InvalidScriptComponent.h"
#include "FoliageComponent.h"
#include "BehaviorTreeComponent.h"
#include "PlayerInput.h"
#include "Animation.h"

void ComponentFactory::Initialize()
{
   auto& registerMap = Meta::MetaDataRegistry->map;

   for (const auto& [name, type] : registerMap)
   {
	   if (name == "Component" || name == "ScriptComponent" || name == "InvalidScriptComponent")
	   {
		   continue; // Skip base Component and ScriptComponent
	   }

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

void ComponentFactory::LoadComponent(GameObject* obj, const MetaYml::detail::iterator_value& itNode, bool isEditorToGame)
{
	if (itNode["ModuleBehavior"])
	{
		std::string scriptName = itNode["m_name"].as<std::string>();
		if (!ScriptManager->IsScriptExists(scriptName))
		{
			auto invalidComponent = obj->AddComponent<InvalidScriptComponent>();
			return;
		}

		auto scriptComponent = obj->AddScriptComponent(scriptName);
		const auto& scriptType = scriptComponent->ScriptReflect();
		Meta::Deserialize(reinterpret_cast<void*>(scriptComponent), scriptType, itNode);

		if (isEditorToGame)
		{
			scriptComponent->MakeInstanceID();
		}
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
			std::string materialName{};
            auto meshRenderer = static_cast<MeshRenderer*>(component);
            Model* model = nullptr;
			MaterialRenderingMode renderingMode = MaterialRenderingMode::Opaque;
            Meta::Deserialize(meshRenderer, itNode);
            if (itNode["m_Material"])
            {
                auto materialNode = itNode["m_Material"];
				materialName = materialNode["m_name"].as<std::string>();
                FileGuid guid = materialNode["m_fileGuid"].as<std::string>();
                model = DataSystems->LoadModelGUID(guid);
				if (materialNode["m_renderingMode"])
				{
					renderingMode = static_cast<MaterialRenderingMode>(materialNode["m_renderingMode"].as<int>());
				}
            }
            MetaYml::Node getMeshNode = itNode["m_Mesh"];
            if (model && getMeshNode)
            {
				Material* matPtr = model->GetMaterial(getMeshNode["m_materialIndex"].as<int>());
				if (matPtr)
				{
					meshRenderer->m_Material = DataSystems->LoadMaterial(materialName);
					if (isEditorToGame || !meshRenderer->m_Material)
					{
						meshRenderer->m_Material = matPtr->Instantiate(matPtr, materialName.empty() ? "Default Material Instnaced" : materialName);
						materialName = meshRenderer->m_Material->m_name;
					}
					meshRenderer->m_Material->m_renderingMode = renderingMode;

					if (itNode["m_Material"])
					{
						auto& materialNode = itNode["m_Material"];
						Meta::Deserialize(meshRenderer->m_Material, materialNode);
						meshRenderer->m_Material->m_name = materialName;
					}
				}

				meshRenderer->m_Mesh = model->GetMesh(getMeshNode["m_name"].as<std::string>());
				if (meshRenderer->m_Mesh)
				{
					MetaYml::Node getLOD_Node = getMeshNode["m_LODThresholds"];
					if (getLOD_Node)
					{
						std::vector<float> lodThresholds;
						for (const auto& threshold : getLOD_Node)
						{
							lodThresholds.push_back(threshold.as<float>());
						}
						meshRenderer->m_Mesh->GenerateLODs(lodThresholds);
					}
				}
            }
			meshRenderer->SetOwner(obj);
            meshRenderer->SetEnabled(true);
        }
		else if (componentType->typeID == type_guid(Animator))
		{
			auto animator = static_cast<Animator*>(component);
			Model* model = nullptr;
			std::vector<bool> animationBools;
			std::unordered_map<int, std::vector<KeyFrameEvent>> animationKeyFrameMap;
			int aniIndex = 0;
			if (itNode["m_Skeleton"])
			{
				auto& skel = itNode["m_Skeleton"];
				if (skel["m_animations"])
				{
					auto& animations = skel["m_animations"];
					for (auto& animation : animations)
					{
						bool _aniBool = animation["m_isLoop"].as<bool>();
						animationBools.push_back(_aniBool);
						auto& keyFrameEvents = animation["m_keyFrameEvent"];
						std::vector<KeyFrameEvent> KeyFrameEventVec;
						for (auto& keyFrameEvent : keyFrameEvents)
						{
							KeyFrameEvent newEvent;
							Meta::Deserialize(&newEvent, keyFrameEvent);
							KeyFrameEventVec.push_back(newEvent);
						}
						if (!KeyFrameEventVec.empty())
						{
							animationKeyFrameMap[aniIndex] = KeyFrameEventVec;
						}
						aniIndex++;
					}
				}
			}

			if (itNode["m_Motion"])
			{
				FileGuid guid = itNode["m_Motion"].as<std::string>();
				if(guid != nullFileGuid)
				{
					animator->m_Motion = guid;
					auto model = DataSystems->LoadModelGUID(guid);
					if(model)
					{
						animator->m_Skeleton = model->m_Skeleton;
					}

					for (int i = 0; i < animator->m_Skeleton->m_animations.size(); ++i)
					{
						animator->m_Skeleton->m_animations[i].m_isLoop = animationBools[i];

						for (auto& event : animationKeyFrameMap[i])
							animator->m_Skeleton->m_animations[i].AddEvent(event);
					}
				}
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
					std::shared_ptr<AnimationController> animationController = std::make_shared<AnimationController>();
					Meta::Deserialize(animationController.get(), layer);
					animationController->m_owner = animator;
					animationController->m_nodeEditor = new NodeEditor();
					if (animationController->useMask == true)
					{
						if (layer["m_avatarMask"])
						{
							auto& MaskNode = layer["m_avatarMask"];
							AvatarMask avatarMask;
							Meta::Deserialize(&avatarMask, MaskNode);
							avatarMask.RootMask = avatarMask.MakeBoneMask(animationController->m_owner->m_Skeleton->m_rootBone);
							if (MaskNode["m_BoneMasks"])
							{
								auto& boneMaskNode = MaskNode["m_BoneMasks"];
								int i = 0;

								for (auto& boneMask : boneMaskNode)
								{
									BoneMask* newboneMask = new BoneMask();
									Meta::Deserialize(newboneMask, boneMask);
									avatarMask.m_BoneMasks[i]->isEnabled = newboneMask->isEnabled;
									i++;
								}
							}
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
							animationController->StateNameSet.insert(sharedState->m_name);
							animationController->States.insert(std::make_pair(sharedState->m_name, animationController->StateVec.size() - 1));
							sharedState->m_ownerController = animationController.get();
							sharedState->SetBehaviour(sharedState->behaviourName);
							if (state["Transitions"])
							{
								auto& transitionNode = state["Transitions"];
								
								for (auto& transition : transitionNode)
								{
									std::shared_ptr<AniTransition> sharedTransition = std::make_shared<AniTransition>();
									Meta::Deserialize(sharedTransition.get(), transition);
									sharedState->Transitions.push_back(sharedTransition);
									sharedTransition->m_ownerController = animationController.get();
								
									if (transition["conditions"])
									{
										auto& conditionNode = transition["conditions"];
										for (auto& condition : conditionNode)
										{
											TransCondition newcondition;
											Meta::Deserialize(&newcondition, condition);

											newcondition.m_ownerController = animationController.get();
											newcondition.SetValue(newcondition.valueName);
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
						if (curNode.IsNull() == false)
						{
							std::string name = curNode["m_name"].as<std::string>();
							animationController->SetCurState(name);
						}
						//animationController->m_curState = animationController->FindState(name);
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
		else if (componentType->typeID == type_guid(BoxColliderComponent))
		{
			auto boxCollider = static_cast<BoxColliderComponent*>(component);
			Meta::Deserialize(boxCollider, itNode);
			boxCollider->SetOwner(obj);
		}
		else if (componentType->typeID == type_guid(CharacterControllerComponent))
		{
			auto characterController = static_cast<CharacterControllerComponent*>(component);
			Meta::Deserialize(characterController, itNode);
			characterController->SetOwner(obj);
		}
		else if (componentType->typeID == type_guid(FoliageComponent))
		{
			auto foliage = static_cast<FoliageComponent*>(component);

			MetaYml::Node FoliageGuid = itNode["m_foliageAssetGuid"];
			if(FoliageGuid.IsNull())
			{
				Debug->LogError("FoliageComponent is missing m_foliageAssetGuid");
				return;
			}

			foliage->m_foliageAssetGuid = FoliageGuid.as<std::string>();

			foliage->LoadFoliageAsset(foliage->m_foliageAssetGuid);

			auto& types = const_cast<std::vector<FoliageType>&>(foliage->GetFoliageTypes());
			for (auto& type : types)
			{
				if (type.m_modelName.empty())
					continue;

				Model* model = nullptr;
				std::array<std::string, 5> exts{ ".fbx", ".gltf", ".glb", ".obj", ".asset" };
				for (const auto& ext : exts)
				{
					auto path = PathFinder::Relative("Models\\" + type.m_modelName + ext);
					if (std::filesystem::exists(path))
					{
						model = DataSystems->LoadCashedModel(path.string());
						break;
					}
				}
				if (!model)
				{
					Debug->LogError("Failed to load model for FoliageType: " + type.m_modelName);
					continue;
				}
				if (model)
				{
					type.m_mesh = model->GetMesh(0);
					type.m_material = model->GetMaterial(0);
				}
			}

			MetaYml::Node foliageInstanceNode = itNode["m_foliageInstances"];

			for(auto& instance : foliage->GetFoliageInstances())
			{
				Mathf::Matrix modelMatrix = Mathf::xMatrixIdentity;
				Mathf::Vector3 position = instance.m_position;
				Mathf::Vector3 rotation = instance.m_rotation;
				Mathf::Vector3 scale = instance.m_scale;

				modelMatrix = Mathf::Matrix::CreateScale(scale) *
					Mathf::Matrix::CreateRotationX(Mathf::ToRadians(rotation.x)) *
					Mathf::Matrix::CreateRotationY(Mathf::ToRadians(rotation.y)) *
					Mathf::Matrix::CreateRotationZ(Mathf::ToRadians(rotation.z)) *
					Mathf::Matrix::CreateTranslation(position);

				const_cast<Mathf::xMatrix&>(instance.m_worldMatrix) = modelMatrix;
			}


			foliage->SetOwner(obj);
			foliage->SetEnabled(true);
		}
		else if(componentType->typeID == type_guid(TerrainComponent))
		{
			auto terrain = static_cast<TerrainComponent*>(component);
			Meta::Deserialize(terrain, itNode);
			terrain->SetOwner(obj);
			if (itNode["m_trrainAssetGuid"])
			{
				FileGuid guid = itNode["m_trrainAssetGuid"].as<std::string>();
				if (guid != nullFileGuid)
				{
					terrain->m_trrainAssetGuid = guid;
					auto path = DataSystems->GetFilePath(guid);
					terrain->Load(path);
				}
			}
			else
			{
				Debug->LogError("Terrain component is missing m_trrainAssetGuid");
			}
		}
		else if (componentType->typeID == type_guid(BehaviorTreeComponent))
		{
			auto behaviorTree = static_cast<BehaviorTreeComponent*>(component);
			Meta::Deserialize(behaviorTree, itNode);
			behaviorTree->SetOwner(obj);
		}
		else if (componentType->typeID == type_guid(PlayerInputComponent))
		{
			auto playerinput = static_cast<PlayerInputComponent*>(component);
			Meta::Deserialize(playerinput, itNode);
			playerinput->SetOwner(obj);

			
			//playerinput->SetActionMap(playerinput->m_actionMapName);
			}
		else
		{
            Meta::Deserialize(reinterpret_cast<void*>(component), *componentType, itNode);
			component->SetOwner(obj);
		}

		if (isEditorToGame)
		{
			component->MakeInstanceID();
		}
    }
}
