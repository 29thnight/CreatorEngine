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
#include "ImageComponent.h"
#include "TextComponent.h"
#include "SpriteSheetComponent.h"
#include "PlayerInput.h"
#include "Animation.h"
#include "Canvas.h"
#include "UIManager.h"

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
                    if (!meshRenderer->m_Material)
                    {
                        meshRenderer->m_Material = matPtr;
                    }
                    meshRenderer->m_Material->m_renderingMode = renderingMode;
                    if (itNode["m_Material"])
                    {
                        auto& materialNode = itNode["m_Material"];
                        Meta::Deserialize(meshRenderer->m_Material, materialNode);
                        if (!materialName.empty())
                            meshRenderer->m_Material->m_name = materialName;

                        auto loadTex = [](const std::string& texName, Texture*& texPtr, bool compress = false)
                        {
                            if (!texName.empty())
                            {
                                texPtr = DataSystems->LoadMaterialTexture(texName, compress);
                            }
                        };

                        loadTex(meshRenderer->m_Material->m_baseColorTexName, meshRenderer->m_Material->m_pBaseColor, true);
                        loadTex(meshRenderer->m_Material->m_normalTexName, meshRenderer->m_Material->m_pNormal);
                        loadTex(meshRenderer->m_Material->m_ORM_TexName, meshRenderer->m_Material->m_pOccRoughMetal);
                        loadTex(meshRenderer->m_Material->m_AO_TexName, meshRenderer->m_Material->m_AOMap);
                        loadTex(meshRenderer->m_Material->m_EmissiveTexName, meshRenderer->m_Material->m_pEmissive);
                    
						if (materialNode["m_shaderPSOName"])
						{
							std::string shaderName = materialNode["m_shaderPSOName"].as<std::string>();
							if (!shaderName.empty())
							{
								auto shaderPso = ShaderSystem->ShaderAssets[shaderName];
								if (shaderPso)
								{
									meshRenderer->m_Material->m_shaderPSOName = shaderName;
									meshRenderer->m_Material->SetShaderPSO(shaderPso);
								}
							}
						}
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
			std::shared_ptr<Texture> texture = nullptr;
			if (itNode["m_Sprite"])
			{
				auto spriteNode = itNode["m_Sprite"];
				FileGuid guid = spriteNode["m_fileGuid"].as<std::string>();
			}
            Meta::Deserialize(spriteRenderer, itNode);
			spriteRenderer->SetOwner(obj);
			spriteRenderer->SetEnabled(true);

			if (spriteRenderer->m_SpritePath != "")
			{
				texture = DataSystems->LoadSharedTexture(spriteRenderer->m_SpritePath, DataSystem::TextureFileType::Texture);
				if (texture)
				{
					spriteRenderer->SetSprite(std::shared_ptr<Texture>(texture));
				}
			}

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
		else if (componentType->typeID == type_guid(Canvas))
		{
			auto canvas = static_cast<Canvas*>(component);
			Meta::Deserialize(canvas, itNode);
			canvas->SetOwner(obj);
			UIManagers->AddCanvas(obj->shared_from_this());
		}
		else if (componentType->typeID == type_guid(ImageComponent))
		{
			auto image = static_cast<ImageComponent*>(component);
			Meta::Deserialize(image, itNode);
			image->SetOwner(obj);

			auto canvasObj = UIManagers->FindCanvasName(image->m_ownerCanvasName);
			auto canvas = canvasObj->GetComponent<Canvas>();
			if (canvas)
			{
				canvas->AddUIObject(obj->shared_from_this());
			}
			else
			{
				Debug->LogWarning("Image Component's parent is not Canvas");
			}

			for(auto& imagePath : image->GetTexturePaths())
			{
				if(imagePath.empty())
					continue;

				auto texture = DataSystems->LoadSharedTexture(imagePath, 
					DataSystem::TextureFileType::UITexture);

				if (!texture)
				{
					Debug->LogError("Failed to load texture for ImageComponent: " + imagePath);
					continue;
				}
				image->DeserializeTexture(texture);
				image->DeserializeNavi();
				image->DeserializeShader();
			}

		}
		else if (componentType->typeID == type_guid(TextComponent))
		{
			auto text = static_cast<TextComponent*>(component);
			Meta::Deserialize(text, itNode);
			text->SetOwner(obj);

			auto canvasObj = UIManagers->FindCanvasName(text->m_ownerCanvasName);
			auto canvas = canvasObj->GetComponent<Canvas>();
			if (canvas)
			{
				canvas->AddUIObject(obj->shared_from_this());
			}
			else
			{
				Debug->LogWarning("Image Component's parent is not Canvas");
			}

			std::string fontPath = text->GetFontPath();
			if (!fontPath.empty())
			{
				text->SetFont(fontPath);
			}
			else
			{
				Debug->LogError("Text Component is missing font path");
			}

			text->DeserializeNavi();
			text->DeserializeShader();
		}
		else if (componentType->typeID == type_guid(SpriteSheetComponent))
		{
			auto spriteSheet = static_cast<SpriteSheetComponent*>(component);
			Meta::Deserialize(spriteSheet, itNode);
			spriteSheet->SetOwner(obj);
			spriteSheet->m_isPreview = false;

			auto canvasObj = UIManagers->FindCanvasName(spriteSheet->m_ownerCanvasName);
			auto canvas = canvasObj->GetComponent<Canvas>();
			if (canvas)
			{
				canvas->AddUIObject(obj->shared_from_this());
			}
			else
			{
				Debug->LogWarning("Image Component's parent is not Canvas");
			}

			if (!spriteSheet->m_spriteSheetPath.empty())
			{
				spriteSheet->LoadSpriteSheet(spriteSheet->m_spriteSheetPath);
			}
			else
			{
				Debug->LogError("SpriteSheetComponent is missing m_spriteSheetPath");
			}
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
