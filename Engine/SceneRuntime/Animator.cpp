#include "Animator.h"
#include "AuthoringNodeViewAccess.h" // D3-a-4
#include "Interfaces/AssetAuthoringPort.h"
#include "AnimatorSystem.h"
#include "Model.h"
#include "TransCondition.h"
#include "AniTransition.h"
#include "AnimationState.h"
#include "AvatarMask.h"
#include "ConditionParameter.h"
#include "ReflectionYml.h"
#include "DataSystem.h"
#include "AnimationController.h"
#include "RenderScene.h"
#include "../RenderEngine/Skeleton.h"
#include "SceneManager.h"
#include "Socket.h"
#include "../RenderEngine/Experiment/Model.h" // I5-D4e-1
#include <nlohmann/json.hpp>

Animator::~Animator()
{
	m_animationControllers.clear();

	{
		std::unique_lock lock(m_paramMutex);
		for (auto& param : Parameters)
		{
			delete param; // 하나씩 해제
		}
		Parameters.clear(); // 벡터 비우기
	}

	for (auto& socket : socketvec)
	{
		delete socket;
	}
	socketvec.clear();
}

namespace
{
	// I5-D4e-1 — legacy MarkRegionSkeleton 재현. 게시 계약(parent < index) 덕에
	// "이름 매칭이면 자기 리전, 아니면 부모 리전 상속"의 단일 순회가 legacy의
	// 서브트리 전파(+나중 매칭이 덮어씀)와 같은 결과를 낸다 — legacy m_bones
	// 순서가 곧 인덱스 순서(역브리지 1:1)이기 때문이다.
	[[nodiscard]] std::vector<std::uint8_t> DeriveExperimentBoneRegions(
		const experiment::Skeleton& skeleton)
	{
		std::vector<std::uint8_t> regions(skeleton.bones.size(),
			static_cast<std::uint8_t>(BoneRegion::Root));
		for (std::size_t index = 0; index < skeleton.bones.size(); ++index)
		{
			const experiment::Bone& bone = skeleton.bones[index];
			const std::string name = ToLower(bone.name);
			bool matched = true;
			BoneRegion region = BoneRegion::Root;
			if (name.find("spine") != std::string::npos) region = BoneRegion::Spine;
			else if (name.find("neck") != std::string::npos) region = BoneRegion::Neck;
			else if (name.find("shoulder") != std::string::npos)
			{
				if (name.find("left") != std::string::npos) region = BoneRegion::LeftArm;
				else if (name.find("right") != std::string::npos) region = BoneRegion::RightArm;
				else matched = false;
			}
			else if (name.find("leg") != std::string::npos)
			{
				if (name.find("left") != std::string::npos) region = BoneRegion::LeftLeg;
				else if (name.find("right") != std::string::npos) region = BoneRegion::RightLeg;
				else matched = false;
			}
			else matched = false;

			if (matched)
			{
				regions[index] = static_cast<std::uint8_t>(region);
			}
			else if (bone.parent.IsValid()
				&& bone.parent.Value() < regions.size())
			{
				regions[index] = regions[bone.parent.Value()];
			}
		}
		return regions;
	}
}

void Animator::EnsureExperimentAnimationBinding()
{
	// I5-D4e-1 — 재생 데이터의 experiment 핸들. m_Motion(모델 GUID — 역브리지
	// 폴백 규약)이 정본 창구다. legacy와 본·클립 계수가 어긋나면 핸들을 비워
	// legacy 틱으로 폴백한다(반쪽 소비 금지 — 인덱스 1:1 계약이 전제라서다).
	m_experimentModel.reset();
	m_experimentBoneRegions.clear();
	if (FileGuid{} == m_Motion || nullptr == m_Skeleton) return;

	std::shared_ptr<const experiment::Model> source =
		DataSystems->TryGetExperimentModel(m_Motion);
	if (nullptr == source) return;
	const experiment::Skeleton* skeleton = source->TryGetSkeleton();
	if (nullptr == skeleton
		|| skeleton->bones.size() != m_Skeleton->m_bones.size()
		|| skeleton->clips.size() != m_Skeleton->m_animations.size())
	{
		return;
	}
	m_experimentBoneRegions = DeriveExperimentBoneRegions(*skeleton);
	m_experimentModel = std::move(source);
}

void Animator::OnInitialized()
{
	auto renderScene = SceneManagers->GetRenderScene();
	if (renderScene)
	{
		renderScene->RegisterAnimator(this);
	}
}

void Animator::OnUninitializing()
{
	auto scene = GetOwner()->m_ownerScene;
	auto renderScene = SceneManagers->GetRenderScene();
	if (renderScene)
	{
		renderScene->UnregisterAnimator(this);
	}
}

// 트랙 C3 — AnimatorSystem 등록/해지. Awake/OnDestroy(컴포넌트당 1회 게이트)가
// 아니라 씬 편입/이탈 훅을 쓰는 이유는 AnimatorSystem.h 상단 주석 참조 — DDOL
// 오브젝트가 씬을 건널 때도 매번 다시 불려야 하기 때문이다. 실제 파괴 경로
// (Scene::FlushPendingDestroy)??OnUninitializing(??OnDestroy 브리지) 직전??
// OnRemovingFromScene을 먼저 부르므로, 이 시스템에서 빠지는 시점이 항상 실
// 파괴보다 먼저다.
void Animator::OnAddedToScene()
{
	AnimatorSystems->Register(this);
}

void Animator::OnRemovingFromScene()
{
	AnimatorSystems->Unregister(this);
}

void Animator::SetAnimation(int index)
{
	m_AnimIndex = index;
	UpdateAnimation();
}

void Animator::UpdateAnimation()
{

	if (m_AnimIndex <= 0)
		m_AnimIndex = 0;

	if (m_AnimIndex >= m_Skeleton->m_animations.size())
		m_AnimIndex = m_Skeleton->m_animations.size() - 1;

	m_AnimIndexChosen = m_AnimIndex;
	m_TimeElapsed = 0;
	
}

void Animator::CreateController(std::string name)
{
	
	std::shared_ptr<AnimationController> animationController = std::make_shared<AnimationController>();
	//AnimationController* animationController = new AnimationController();
	animationController->m_owner = this;
	animationController->name = name;
	animationController->CreateState("Ani State", -1, true);
	m_animationControllers.push_back(animationController);
}

std::shared_ptr<AnimationController> Animator::CreateController_UI()
{
	std::shared_ptr<AnimationController> animationController = std::make_shared<AnimationController>();
	//AnimationController* animationController = new AnimationController();
	animationController->m_owner = this;
	animationController->name = "NewLayer" + std::to_string(m_animationControllers.size());
	animationController->CreateState("Ani State",-1,true);
	m_animationControllers.push_back(animationController);
	return animationController;
}
std::shared_ptr<AnimationController> Animator::CreateController_UINoAni()
{
	std::shared_ptr<AnimationController> animationController = std::make_shared<AnimationController>();
	//AnimationController* animationController = new AnimationController();
	animationController->m_owner = this;
	animationController->name = "NewLayer" + std::to_string(m_animationControllers.size());
	//animationController->CreateState("Ani State", -1, true);
	m_animationControllers.push_back(animationController);
	return animationController;
}

void Animator::DeleteController(int index)
{	
	m_animationControllers.erase(m_animationControllers.begin() + index);
}

void Animator::DeleteController(std::string controllerName)
{
	auto it = std::remove_if(m_animationControllers.begin(), m_animationControllers.end(),
		[&](std::shared_ptr<AnimationController> controller)
		{
				return controller->name == controllerName;
		});

	m_animationControllers.erase(it, m_animationControllers.end()); 
	
}

AnimationController* Animator::GetController(std::string name)
{
    for (const auto Controller : m_animationControllers)
    {
            if (Controller->name == name)
                    return Controller.get();
    }
    return nullptr;
}



void Animator::SetUseLayer(int layerindex, bool _useLayer)
{
	if (layerindex >= 0 && layerindex < m_animationControllers.size())
	{
		m_animationControllers[layerindex]->SetUseLayer(_useLayer);
	}
}

Entity* Animator::FindBoneRecursive(Entity* parent, const std::string& boneName)
{
	if (!parent) return nullptr;

	for (Entity::Index childIndex : parent->GetChildrenIndices())
	{
		Entity* child = parent->OwnerSceneFindIndex(childIndex);
		if (!child) continue;

		if (child->m_name == boneName)
			return child;

		// 자식의 자식들도 탐색
		if (Entity* result = FindBoneRecursive(child, boneName))
			return result;
	}

	return nullptr;
}

Socket* Animator::MakeSocket(std::string_view socketName, std::string_view boneName, Entity* object)
{
	if (Socket* socket = FindSocket(socketName); socket)
		return socket;

	// 먼저 자식 구조 전체에서 boneName을 찾는다 (재귀적 탐색)
	std::string realBoneName = boneName.data();
	Entity* socketBone = FindBoneRecursive(object, realBoneName);

	// 없으면 (1)~(100)까지 이름 붙여서 찾는다
	int index = 1;
	while (!socketBone && index <= 10)
	{
		std::string indexedName = realBoneName + " (" + std::to_string(index) + ")";
		socketBone = FindBoneRecursive(object, indexedName);
		if (socketBone)
		{
			realBoneName = indexedName;  // 실제 본 이름 업데이트
			break;
		}
		++index;
	}

	// 찾았으면 소켓 생성 후 반환
	if (socketBone)
	{
		Socket* newSocket = new Socket();
		newSocket->m_name = socketName;
		newSocket->GameObjectIndex = 9999 + index; // 임의의 인덱스
		newSocket->m_ObjectName = boneName;
		socketvec.push_back(newSocket);
		return newSocket;
	}
	return nullptr;
}

Socket* Animator::FindSocket(std::string_view socketName)
{
	for (const auto socket : socketvec)
	{
		if (socket->m_name == socketName)
			return socket;
	}

	return nullptr;
}

void Animator::ClearControllersAndParams()
{
	m_animationControllers.clear();

	std::unique_lock lock(m_paramMutex);
	for (auto* p : Parameters)
	{
		delete p;  
	}
	Parameters.clear();
}

void Animator::DeleteParameter(int index)
{
	std::unique_lock lock(m_paramMutex);
	if (index >= 0 && index < Parameters.size())
	{
		for (const auto controller : m_animationControllers)
		{
			for (const auto state : controller->StateVec)
			{
				for (const auto transition : state->Transitions)
				{
					for (auto& condition : transition->conditions)
					{
						if (condition.valueParameter == Parameters[index])
						{
							condition.valueParameter = nullptr;
						}
					}
				}

			}
		}
		delete Parameters[index];
		Parameters.erase(Parameters.begin() + index);
	}
}

ConditionParameter* Animator::AddDefaultParameter(ValueType vType)
{
	std::string baseName;
	switch (vType)
	{
	case ValueType::Float:
		baseName = "NewFloat";
		break;
	case ValueType::Int:
		baseName = "NewInt";
		break;
	case ValueType::Bool:
		baseName = "NewBool";
		break;
	case ValueType::Trigger:
		baseName = "NewTrigger";
		break;
	}
	std::string valueName = baseName;
	int index = 0;
	bool isDuplicate = true;
	{
		std::unique_lock lock(m_paramMutex);
		while (isDuplicate)
		{
			isDuplicate = false;
			for (const auto parm : Parameters)
			{
				if (parm->name == valueName)
				{
					isDuplicate = true;
					valueName = baseName + std::to_string(++index);
					break;
				}
			}
		}
	}
	ConditionParameter* newParameter = new ConditionParameter(0, vType, valueName);
	{
		std::unique_lock lock(m_paramMutex);
		Parameters.push_back(newParameter);
	}
	return newParameter;
}

ConditionParameter* Animator::FindParameter(std::string valueName)
{
	std::unique_lock lock(m_paramMutex);
	for (const auto parameter : Parameters)
	{
		if (parameter->name == valueName)
		{
			return parameter;
		}
	}
	return nullptr; 
}




bool Animator::SerializeControllers(std::string _jsonName)
{
	if (_jsonName.empty())
	{
		Debug->LogError("Animator controller save requires a non-empty name");
		return false;
	}

	nlohmann::json json;
	nlohmann::json controllerArray = nlohmann::json::array();
	for (const auto Controller : m_animationControllers)
	{
		controllerArray.push_back(Controller->Serialize());
	}
	json["Controllers"] = controllerArray;
	nlohmann::json paramArray = nlohmann::json::array();

	{
		std::unique_lock lock(m_paramMutex);
		for (const auto param : Parameters)
		{
			paramArray.push_back(param->Serialize());
		}
	}
	json["Parameters"] = paramArray;

	// replace_extension은 이름에 '.'이 있으면 그 뒤를 통째로 잘라낸다("v1.2" →
	// "v1.json"). 문자열로 붙여 이름을 보존한다.
	UncatalogedAuthoringRequest request{};
	request.destinationPath = PathFinder::AnimatorjsonPath(_jsonName + ".json");
	request.payload = json.dump(4);

	if (!AssetAuthoringPort::WriteAnimatorController(request))
	{
		Debug->LogError(
			"Animator controller save requires a complete Editor authoring "
			"transaction: " + _jsonName);
		return false;
	}

	return true;
}

void Animator::DeserializeControllers(std::string _filename)
{
	//폴더열어서 json 선택
	/*namespace fs = std::filesystem;
	fs::path dirPath = PathFinder::AnimatorjsonPath();
	if (!fs::exists(dirPath) || !fs::is_directory(dirPath))
	{
		std::cerr << "Directory does not exist: " << dirPath << std::endl;
		return;
	}

	for (const auto& entry : fs::directory_iterator(dirPath))
	{
		if (entry.is_regular_file() && entry.path().extension() == ".json")
		{
			std::string filePath = entry.path().string();


			

		}
	}*/
	std::ifstream file(_filename);
	if (!file.is_open())
	{
		std::cerr << "Failed to open: " << _filename << std::endl;
	}

	nlohmann::json json;
	try 
	{
		file >> json;
	}
	catch (std::exception& e) 
	{
		std::cerr << "JSON parsing error: " << e.what() << std::endl;
	}

	ClearControllersAndParams();
	for (const auto parameterJson : json["Parameters"])
	{
		int param_vType = parameterJson["param_vType"];
		ValueType paramvType = ValueType::Float;
		switch (param_vType)
		{
		case 0:
			paramvType = ValueType::Float;
			break;
		case 1:
			paramvType = ValueType::Int;
			break;
		case 2:
			paramvType = ValueType::Bool;
			break;
		case 3:
			paramvType = ValueType::Trigger;
			break;
		}

		ConditionParameter* param = AddDefaultParameter(paramvType);
		param->name = parameterJson["param_name"];
	}

	for (const auto contorllerJson : json["Controllers"])
	{

		std::shared_ptr<AnimationController> curController = CreateController_UINoAni();

		curController->name = contorllerJson["controller_name"];

		bool usecontroller = false;
		bool usemask = false;
		if (contorllerJson["useController"] == 1)
			usecontroller = true;
		curController->useController = usecontroller;
		/*if (contorllerJson["useMask"] == 1)
			usemask = true;*/
		//curController->useMask = usemask;

		for (const auto stateJson : contorllerJson["StateVec"])
		{
			std::shared_ptr<AnimationState> curState = curController->CreateState_UI();
			curState->AnimationIndex = stateJson["animationIndex"];
			curState->animationSpeed = stateJson["animationSpeed"];
			curState->animationSpeedParameterName = stateJson["animationSpeedParameterName"];
			curState->behaviourName = stateJson["behaviourName"];
			curState->SetBehaviour(curState->behaviourName);
			curState->multiplerAnimationSpeed = stateJson["multiplerAnimationSpeed"];
			curState->m_name = stateJson["state_name"];
			curState->m_isAny = stateJson["m_isAny"];
			bool usemultipler = false;
			if (stateJson["useMultipler"] == 1)
			{
				usemultipler = true;
			}
			curState->useMultipler = usemultipler;

		}

		for (const auto stateJson : contorllerJson["StateVec"])
		{
			for (const auto transionJson : stateJson["transitions"])
			{
				std::string curstateName = transionJson["curStateName"];
				std::string nextStateName = transionJson["nextStateName"];
				AniTransition* curtrans = curController->CreateTransition(curstateName, nextStateName);
				bool hasexitTime = false;
				if (transionJson["hasExitTime"] == 1)
					hasexitTime = true;
				curtrans->hasExitTime = hasexitTime;
				curtrans->exitTime = transionJson["exitTime"];
				curtrans->blendTime = transionJson["blendTime"];

				for (const auto condionJson : transionJson["conditions"])
				{
					auto firstParam = Parameters[0];
					TransCondition* curcodition = curtrans->AddConditionDefault(firstParam->name, ConditionType::None, firstParam->vType);
					curcodition->SetCondition(condionJson["valueName"]);
					ConditionType ctype = ConditionType::Greater;

					int type = condionJson["cType"].get<int>();

					switch (type)
					{
					case 1:
						ctype = ConditionType::Less;
						break;
					case 2:
						ctype = ConditionType::Equal;
						break;
					case 3:
						ctype = ConditionType::NotEqual;
						break;
					case 4:
						ctype = ConditionType::True;
						break;
					case 5:
						ctype = ConditionType::False;
						break;
					default:
						ctype = ConditionType::Less;
						break;
					}
					curcodition->cType = ctype;

					curcodition->CompareParameter.fValue = condionJson["fValue"];
					curcodition->CompareParameter.iValue = condionJson["iValue"];
					curcodition->CompareParameter.bValue = condionJson["bValue"];
					curcodition->CompareParameter.tValue = condionJson["tValue"];

				}
			}
		}
		curController->SetCurState(contorllerJson["m_curState"]);
	}
}

void Animator::OnDeserialized(const Authoring::NodeView& view)
{
	const Authoring::ReadNode node = Authoring::NodeViewAccess::Node(view);
	// CT6-d: 구 ComponentFactory Animator 분기 이동(동작·순서 보존).
	// Parameters·m_animationControllers는 포인터 원소 벡터라 typed 역직렬화가
	// 건드리지 않는다 — 여기의 수동 복원이 실채움이다.
	Model* model = nullptr;
	std::vector<bool> animationBools;
	std::unordered_map<int, std::vector<KeyFrameEvent>> animationKeyFrameMap;
	int aniIndex = 0;
	if (node["m_Skeleton"])
	{
		const auto skel = node["m_Skeleton"];
		if (skel["m_animations"])
		{
			const auto animations = skel["m_animations"];
			for (const auto animation : animations)
			{
				bool _aniBool = animation["m_isLoop"].As<bool>();
				animationBools.push_back(_aniBool);
				const auto keyFrameEvents = animation["m_keyFrameEvent"];
				std::vector<KeyFrameEvent> KeyFrameEventVec;
				for (const auto keyFrameEvent : keyFrameEvents)
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

	if (node["m_Motion"])
	{
		FileGuid guid = node["m_Motion"].AsString();
		if (guid != nullFileGuid)
		{
			m_Motion = guid;
			auto model = DataSystems->LoadModelGUID(guid);
			if (model)
			{
				m_Skeleton = model->m_Skeleton;
				// D34b 진단: 복원된 스켈레톤의 루트 부재를 로드 시점에 알린다 —
				// 이대로 틱에 나가면 AnimationJob이 건너뛰며 같은 메시지를 낸다.
				if (m_Skeleton && nullptr == m_Skeleton->m_rootBone)
				{
					Debug->LogError("[Animator] 복원된 스켈레톤에 루트 본이 없다: "
						+ model->name);
				}
			}

			for (int i = 0; i < m_Skeleton->m_animations.size(); ++i)
			{
				if (animationBools.empty()) break;

				m_Skeleton->m_animations[i].m_isLoop = animationBools[i];

				for (const auto event : animationKeyFrameMap[i])
					m_Skeleton->m_animations[i].AddEvent(event);
			}
		}
	}

	if (node["Parameters"])
	{
		const auto paramNode = node["Parameters"];

		for (const auto param : paramNode)
		{
			ConditionParameter* aniParam = new ConditionParameter();
			Meta::Deserialize(aniParam, param);
			Parameters.push_back(aniParam);
		}
	}

	if (node["m_animationControllers"])
	{
		const auto animationControllerNode = node["m_animationControllers"];

		for (const auto layer : animationControllerNode)
		{
			std::shared_ptr<AnimationController> animationController = std::make_shared<AnimationController>();
			Meta::Deserialize(animationController.get(), layer);
			animationController->m_owner = this;
			if (animationController->useMask == true)
			{
				if (layer["m_avatarMask"])
				{
					const auto MaskNode = layer["m_avatarMask"];
					AvatarMask avatarMask;
					Meta::Deserialize(&avatarMask, MaskNode);
					avatarMask.RootMask = avatarMask.MakeBoneMask(animationController->m_owner->m_Skeleton->m_rootBone);
					if (MaskNode["m_BoneMasks"])
					{
						const auto boneMaskNode = MaskNode["m_BoneMasks"];
						int i = 0;

						for (const auto boneMask : boneMaskNode)
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
				const auto StatesNode = layer["StateVec"];
				for (const auto state : StatesNode)
				{
					std::shared_ptr<AnimationState> sharedState = std::make_shared<AnimationState>();
					Meta::Deserialize(sharedState.get(), state);
					animationController->StateVec.push_back(sharedState);
					animationController->StateNameSet.insert(sharedState->m_name);
					animationController->m_nameToState.insert(std::make_pair(sharedState->m_name, sharedState));
					sharedState->m_ownerController = animationController.get();
					sharedState->SetBehaviour(sharedState->behaviourName);
					if (state["Transitions"])
					{
						const auto transitionNode = state["Transitions"];

						for (const auto transition : transitionNode)
						{
							std::shared_ptr<AniTransition> sharedTransition = std::make_shared<AniTransition>();
							Meta::Deserialize(sharedTransition.get(), transition);
							sharedState->Transitions.push_back(sharedTransition);
							sharedTransition->m_ownerController = animationController.get();

							if (transition["conditions"])
							{
								const auto conditionNode = transition["conditions"];
								for (const auto condition : conditionNode)
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
				const auto curNode = layer["m_curState"];
				if (curNode.IsNull() == false)
				{
					std::string name = curNode["m_name"].AsString();
					animationController->SetCurState(name);
				}
			}

			for (const auto state : animationController->StateVec)
			{
				for (const auto transition : state->Transitions)
				{
					transition->SetCurState(transition->curStateName);
					transition->SetNextState(transition->nextStateName);
				}
			}

			m_animationControllers.push_back(animationController);
		}
	}

	// I5-D4e-1 — 씬 로드 경계에서 experiment 재생 핸들을 잇는다(m_Motion·
	// m_Skeleton이 위에서 복원된 뒤여야 계수 대조가 성립한다).
	EnsureExperimentAnimationBinding();
}

