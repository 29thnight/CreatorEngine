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
	// 폴백 규약)이 정본 창구다.
	//
	// ★ I6-B0 — 바인딩이 legacy에서 자립한다. 예전엔 m_Skeleton이 없으면 즉시
	//   돌아서고 본·클립 계수를 legacy와 대조해 불일치면 핸들을 비웠다 —
	//   공유 자산이 없으면 experiment 경로가 **원리적으로 켜지지 않는** 구조라
	//   legacy 은퇴의 첫 자물쇠였다. 그 대조가 지키던 것(인덱스 1:1)은 이미
	//   상시 게이트가 코퍼스 전수에서 증명한다 — 팔레트 파리티(6)·본 해석(8)·
	//   마스크(9) 오차 0. 런타임 대조는 그 위의 이중 잠금이었고, 은퇴하면
	//   대조할 상대 자체가 사라진다. 대신 experiment 내부 불변식(본이 있다·
	//   루트가 범위 안)을 직접 검사한다 — 독립 유도라야 대조군이 된다.
	m_experimentModel.reset();
	m_experimentBoneRegions.clear();
	if (FileGuid{} == m_Motion) return;

	std::shared_ptr<const experiment::Model> source =
		DataSystems->TryGetExperimentModel(m_Motion);
	if (nullptr == source) return;
	const experiment::Skeleton* skeleton = source->TryGetSkeleton();
	if (nullptr == skeleton || skeleton->bones.empty()
		|| !experiment::IsInRange(skeleton->rootBone, skeleton->bones.size()))
	{
		return;
	}
	m_experimentBoneRegions = DeriveExperimentBoneRegions(*skeleton);
	m_experimentModel = std::move(source);
}

uint64 Animator::GetSkeletonSerial(bool* outViaExperiment) const
{
	// 0은 "스켈레톤 없음" — Scene 본 전파의 가드 값이다(두 축 모두 1 이상에서
	// 센다). I6-B2: experiment 핸들이 있으면 그 generation이 신원이고, legacy
	// serial은 폴백이다. 신원이 legacy 객체 수명에 묶여 있는 한 그 객체를
	// 은퇴시킬 수 없다 — 본 전파가 통째로 꺼지기 때문이다.
	if (outViaExperiment) *outViaExperiment = false;
	if (m_experimentModel)
	{
		if (nullptr != m_experimentModel->TryGetSkeleton())
		{
			if (outViaExperiment) *outViaExperiment = true;
			return m_experimentModel->Generation();
		}
	}
	return m_Skeleton ? m_Skeleton->m_serial : 0;
}

std::size_t Animator::GetBoneCount(bool* outViaExperiment) const
{
	if (outViaExperiment) *outViaExperiment = false;
	if (m_experimentModel)
	{
		if (const experiment::Skeleton* skeleton =
			m_experimentModel->TryGetSkeleton())
		{
			if (outViaExperiment) *outViaExperiment = true;
			return skeleton->bones.size();
		}
	}
	return m_Skeleton ? m_Skeleton->m_bones.size() : 0;
}

std::string Animator::GetBoneName(int boneIndex, bool* outViaExperiment) const
{
	if (outViaExperiment) *outViaExperiment = false;
	if (boneIndex < 0) return std::string{};
	const std::size_t index = static_cast<std::size_t>(boneIndex);
	if (m_experimentModel)
	{
		if (const experiment::Skeleton* skeleton =
			m_experimentModel->TryGetSkeleton())
		{
			if (outViaExperiment) *outViaExperiment = true;
			if (index >= skeleton->bones.size()) return std::string{};
			return skeleton->bones[index].name;
		}
	}
	if (m_Skeleton && index < m_Skeleton->m_bones.size())
	{
		// legacy m_bones는 포인터 배열이라 구멍이 있을 수 있다(실측 전례).
		const Bone* const bone = m_Skeleton->m_bones[index];
		return bone ? bone->m_name : std::string{};
	}
	return std::string{};
}

int Animator::ResolveBoneIndex(const std::string& boneName,
	bool* outViaExperiment) const
{
	if (outViaExperiment) *outViaExperiment = false;
	if (m_experimentModel)
	{
		if (const experiment::Skeleton* skeleton =
			m_experimentModel->TryGetSkeleton())
		{
			if (outViaExperiment) *outViaExperiment = true;
			for (std::size_t index = 0; index < skeleton->bones.size(); ++index)
			{
				if (skeleton->bones[index].name == boneName)
				{
					return static_cast<int>(index);
				}
			}
			return -1;
		}
	}
	if (m_Skeleton)
	{
		Bone* const bone = m_Skeleton->FindBone(boneName);
		return bone ? bone->m_index : -1;
	}
	return -1;
}

BoneMask* Animator::BuildAvatarBoneMasks(AvatarMask& mask,
	bool* outViaExperiment)
{
	if (outViaExperiment) *outViaExperiment = false;
	const experiment::Skeleton* skeleton = m_experimentModel
		? m_experimentModel->TryGetSkeleton() : nullptr;
	if (nullptr == skeleton)
	{
		// legacy 폴백(Assimp 모델) — 구 CreateMask 경로 그대로. region 태깅은
		// 공유 자산(Bone::m_region) 쓰기지만 이름 파생이라 멱등이다.
		if (nullptr == m_Skeleton || nullptr == m_Skeleton->m_rootBone)
		{
			return nullptr;
		}
		m_Skeleton->MarkRegionSkeleton();
		return mask.MakeBoneMask(m_Skeleton->m_rootBone);
	}

	if (outViaExperiment) *outViaExperiment = true;
	// experiment — parent-only 표현에서 children 목록을 만들고 legacy
	// MakeBoneMask 재귀와 같은 DFS 선순(자기 → 자식 인덱스 순)으로 생성한다.
	// ★ m_BoneMasks의 push 순서가 곧 저장분 인덱스 대응(postLoad의
	//   ReCreateMask가 i번째끼리 잇는다)이라 순서 재현이 계약이다.
	const std::size_t boneCount = skeleton->bones.size();
	if (!experiment::IsInRange(skeleton->rootBone, boneCount)) return nullptr;

	std::vector<std::vector<std::uint32_t>> children(boneCount);
	for (std::size_t index = 0; index < boneCount; ++index)
	{
		const experiment::Bone& bone = skeleton->bones[index];
		if (bone.parent.IsValid() && bone.parent.Value() < boneCount)
		{
			children[bone.parent.Value()].push_back(
				static_cast<std::uint32_t>(index));
		}
	}

	std::vector<BoneMask*> masks(boneCount, nullptr);
	std::vector<std::uint32_t> stack{ skeleton->rootBone.Value() };
	while (!stack.empty())
	{
		const std::uint32_t boneIndex = stack.back();
		stack.pop_back();
		const experiment::Bone& bone = skeleton->bones[boneIndex];

		BoneMask* newMask = new BoneMask();
		newMask->boneName = bone.name;
		newMask->isEnabled = true;
		mask.m_BoneMasks.push_back(newMask);
		masks[boneIndex] = newMask;
		if (bone.parent.IsValid() && masks[bone.parent.Value()])
		{
			masks[bone.parent.Value()]->m_children.push_back(newMask);
		}

		// 자식을 역순으로 쌓아야 pop이 인덱스 순으로 방문한다(legacy 재귀의
		// children 순회 순서 재현).
		for (auto childIt = children[boneIndex].rbegin();
			childIt != children[boneIndex].rend(); ++childIt)
		{
			stack.push_back(*childIt);
		}
	}
	return masks[skeleton->rootBone.Value()];
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

	// I6-B0 — 클립 계수는 창구가 정본이다(experiment 우선·legacy 폴백).
	// 이전엔 m_Skeleton을 **가드 없이** 역참조했다 — 모델이 안 붙은 Animator에
	// SetAnimation이 닿으면 그 자리에서 죽는다. 클립이 0이면 선택도 0이다.
	const std::size_t clipCount = GetClipCount();
	if (0 == clipCount)
	{
		m_AnimIndex = 0;
	}
	else if (static_cast<std::size_t>(m_AnimIndex) >= clipCount)
	{
		m_AnimIndex = static_cast<int>(clipCount) - 1;
	}

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
	// I6-B1 — 클립 오버라이드 표기가 Animator 소유로 이주했다. 새 정본을
	// 먼저 읽고, 없을 때만 구 씬의 legacy Skeleton 서브트리를 읽는다. 폴백은
	// **읽기 전용**이다 — 저장은 아래 OnAfterSerialize가 새 표기로만 한다.
	bool authoredOverrides = false;
	if (node["m_clipOverrides"])
	{
		authoredOverrides = true;
		for (const auto entry : node["m_clipOverrides"])
		{
			if (!entry["clipIndex"]) continue;
			const int clipIndex = entry["clipIndex"].As<int>();
			if (clipIndex < 0) continue;
			AnimatorClipOverride& clipOverride = EnsureClipOverride(clipIndex);
			if (entry["loopOverride"])
			{
				clipOverride.loopOverride = entry["loopOverride"].As<bool>();
			}
			if (entry["events"])
			{
				for (const auto keyFrameEvent : entry["events"])
				{
					KeyFrameEvent newEvent;
					Meta::Deserialize(&newEvent, keyFrameEvent);
					clipOverride.events.push_back(newEvent);
				}
			}
		}
	}

	if (!authoredOverrides && node["m_Skeleton"])
	{
		// 구 씬 폴백 — 인덱스는 서브트리의 **순서**가 정한다(이름이 아니다).
		// 그 규약은 저장 당시 자산 클립 순서와 1:1이라는 전제 위에 섰고,
		// 새 표기는 clipIndex를 명시해 그 암묵 전제를 없앤다.
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
		}
	}

	// I5-D4e-2 — 씬이 저장한 클립별 isLoop·이벤트를 **자기 소유 오버라이드**로
	// 보관한다. 구 코드는 공유 자산(m_Skeleton->m_animations)에 재주입해 같은
	// 스켈레톤을 공유하는 Animator 간 오염(마지막 로드 승자)을 만들었다 — 이제
	// 자산은 불변이고, 재생 루프 판정(IsClipLooping)·발화(InvokeClipEvents)·
	// writer(OnAfterSerialize)가 이 오버라이드를 정본으로 본다.
	for (int i = 0; i < static_cast<int>(animationBools.size()); ++i)
	{
		EnsureClipOverride(i).loopOverride = animationBools[i];
	}
	for (auto& [clipIndex, events] : animationKeyFrameMap)
	{
		EnsureClipOverride(clipIndex).events = std::move(events);
	}

	// I5-D4e-1/3 — experiment 재생 핸들. m_Motion·m_Skeleton 복원 직후,
	// 아래 컨트롤러 복원 **이전**이어야 한다 — AvatarMask 재생성
	// (BuildAvatarBoneMasks)이 이 핸들을 보고 경로를 고르기 때문이다(D4e-3
	// 이동 전에는 함수 끝에 있어 마스크가 항상 legacy 폴백을 탔다).
	EnsureExperimentAnimationBinding();

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
					// I5-D4e-3 — 마스크 트리 생성 창구(experiment 정본·legacy 폴백,
					// m_BoneMasks 순서는 legacy DFS 선순 재현 — 저장분 인덱스 대응).
					avatarMask.RootMask = BuildAvatarBoneMasks(avatarMask);
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
}

void Animator::OnAfterSerialize(YAML::Node& node)
{
	// I6-B1 — 오버라이드가 자기 표기를 갖는다. 구 코드는 리플렉션이 legacy
	// Skeleton 포인터를 따라 적은 서브트리에 값을 **되입혔다** — 저장 형상이
	// 은퇴 대상 타입의 형상이라, 그 타입이 죽는 순간 저작분이 갈 곳을 잃는다.
	// 이제 소유(Animator)와 표기가 같은 곳을 가리킨다.
	//
	// ★ 인덱스를 명시한다. 구 표기는 서브트리 **순서**가 인덱스였고, 그것은
	//   저장 당시 자산 클립 순서와 1:1이라는 암묵 전제 위에 섰다.
	//
	// 빈 오버라이드(값도 이벤트도 없는 것)는 적지 않는다 — 왕복 멱등이
	// 이 게이트의 판정이라 쓰는 것과 읽는 것이 정확히 같아야 한다.
	if (m_clipOverrides.empty()) return;

	YAML::Node overridesNode(YAML::NodeType::Sequence);
	for (AnimatorClipOverride& clipOverride : m_clipOverrides)
	{
		if (clipOverride.clipIndex < 0) continue;
		if (!clipOverride.loopOverride.has_value()
			&& clipOverride.events.empty())
		{
			continue;
		}

		YAML::Node entry(YAML::NodeType::Map);
		entry["clipIndex"] = clipOverride.clipIndex;
		if (clipOverride.loopOverride.has_value())
		{
			entry["loopOverride"] = *clipOverride.loopOverride;
		}
		if (!clipOverride.events.empty())
		{
			YAML::Node eventsNode(YAML::NodeType::Sequence);
			for (KeyFrameEvent& event : clipOverride.events)
			{
				eventsNode.push_back(Meta::Serialize(&event));
			}
			entry["events"] = eventsNode;
		}
		overridesNode.push_back(entry);
	}

	if (0 == overridesNode.size()) return;
	node["m_clipOverrides"] = overridesNode;
}

