#include "Animator.h"
#include "AnimationController.h"
#include "RenderScene.h"
#include "../RenderEngine/Skeleton.h"
#include "NodeEditor.h"
#include "SceneManager.h"
#include "Socket.h"
#include <nlohmann/json.hpp>
void Animator::Awake()
{
	auto renderScene = SceneManagers->GetRenderScene();
	if (renderScene)
	{
		auto sharedThis = shared_from_this();
		renderScene->RegisterAnimator(sharedThis);
	}
}

void Animator::Update(float tick)
{
	if (m_animationControllers.empty()) return;

	for (auto& animationController : m_animationControllers)
	{
		animationController->Update(tick);
	}

	for (auto& param : Parameters)
	{
		if (param->vType == ValueType::Trigger)
		{
			param->ResetTrigger();
		}
	}
}

void Animator::OnDestroy()
{
	auto scene = GetOwner()->m_ownerScene;
	auto renderScene = SceneManagers->GetRenderScene();
	if (renderScene)
	{
		auto sharedThis = shared_from_this();
		renderScene->UnregisterAnimator(sharedThis);
	}
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
	animationController->m_nodeEditor = new NodeEditor();
	animationController->CreateState("Ani State", -1, true);
	m_animationControllers.push_back(animationController);
}

std::shared_ptr<AnimationController> Animator::CreateController_UI()
{
	std::shared_ptr<AnimationController> animationController = std::make_shared<AnimationController>();
	//AnimationController* animationController = new AnimationController();
	animationController->m_owner = this;
	animationController->name = "NewLayer" + std::to_string(m_animationControllers.size());
	animationController->m_nodeEditor = new NodeEditor();
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
	animationController->m_nodeEditor = new NodeEditor();
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
    for (auto& Controller : m_animationControllers)
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

GameObject* Animator::FindBoneRecursive(GameObject* parent, const std::string& boneName)
{
	if (!parent) return nullptr;

	for (int childIndex : parent->m_childrenIndices)
	{
		GameObject* child = GameObject::FindIndex(childIndex);
		if (!child) continue;

		if (child->m_name == boneName)
			return child;

		// 자식의 자식들도 탐색
		if (GameObject* result = FindBoneRecursive(child, boneName))
			return result;
	}

	return nullptr;
}

Socket* Animator::MakeSocket(std::string_view socketName, std::string_view boneName, GameObject* object)
{
	if (Socket* socket = FindSocket(socketName); socket)
		return socket;

	// 먼저 자식 구조 전체에서 boneName을 찾는다 (재귀적 탐색)
	std::string realBoneName = boneName.data();
	GameObject* socketBone = FindBoneRecursive(object, realBoneName);

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
	for (auto& socket : socketvec)
	{
		if (socket->m_name == socketName)
			return socket;
	}

	return nullptr;
}

void Animator::ClearControllersAndParams()
{
	m_animationControllers.clear();

	for (auto* p : Parameters)
	{
		delete p;  
	}
	Parameters.clear();
}

void Animator::DeleteParameter(int index)
{
	if (index >= 0 && index < Parameters.size())
	{
		for (auto& controller : m_animationControllers)
		{
			for (auto& state : controller->StateVec)
			{
				for (auto& transition : state->Transitions)
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

ConditionParameter* Animator::FindParameter(std::string valueName)
{
	for (auto& parameter : Parameters)
	{
		if (parameter->name == valueName)
		{
			return parameter;
		}
	}
	return nullptr; 
}




void Animator::SerializeControllers(std::string _jsonName)
{
	nlohmann::json json;
	nlohmann::json controllerArray = nlohmann::json::array();
	for (auto& Controller : m_animationControllers)
	{
		controllerArray.push_back(Controller->Serialize());
	}
	json["Controllers"] = controllerArray;
	nlohmann::json paramArray = nlohmann::json::array();
	for (auto& param : Parameters)
	{
		paramArray.push_back(param->Serialize());
	}
	json["Parameters"] = paramArray;
	file::path filepath = PathFinder::AnimatorjsonPath(_jsonName);
	filepath.replace_extension(".json");
	std::ofstream file(filepath);
	file << json.dump(4);
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
	try {
		file >> json;
	}

	catch (std::exception& e) {
		std::cerr << "JSON parsing error: " << e.what() << std::endl;
	}

	ClearControllersAndParams();
	for (auto& parameterJson : json["Parameters"])
	{
		int param_vType = parameterJson["param_vType"];
		ValueType paramvType = ValueType::Float;
		if (param_vType == 0)
			paramvType = ValueType::Float;
		else if (param_vType == 1)
			paramvType = ValueType::Int;
		else if (param_vType == 2)
			paramvType = ValueType::Bool;
		else if (param_vType == 3)
			paramvType = ValueType::Trigger;
		ConditionParameter* param = AddDefaultParameter(paramvType);
		param->name = parameterJson["param_name"];
	}

	for (auto& contorllerJson : json["Controllers"])
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

		for (auto& stateJson : contorllerJson["StateVec"])
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

		for (auto& stateJson : contorllerJson["StateVec"])
		{
			for (auto& transionJson : stateJson["transitions"])
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

				for (auto& condionJson : transionJson["conditions"])
				{
					auto firstParam = Parameters[0];
					TransCondition* curcodition = curtrans->AddConditionDefault(firstParam->name, ConditionType::None, firstParam->vType);
					curcodition->SetCondition(condionJson["valueName"]);
					ConditionType ctype = ConditionType::Greater;

					if (condionJson["cType"] == 1)
						ctype = ConditionType::Less;
					else if (condionJson["cType"] == 2)
						ctype = ConditionType::Equal;
					else if (condionJson["cType"] == 3)
						ctype = ConditionType::NotEqual;
					else if (condionJson["cType"] == 4)
						ctype = ConditionType::True;
					else if (condionJson["cType"] == 5)
						ctype = ConditionType::False;
					else
						ctype = ConditionType::Less;
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