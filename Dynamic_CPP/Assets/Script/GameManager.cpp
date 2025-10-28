#include "GameManager.h"
#include "pch.h"
#include "SceneManager.h"
#include "Entity.h"
#include "MeshRenderer.h"
#include "Material.h"
#include "MaterialInfomation.h"
#include "InputManager.h"
#include "RigidBodyComponent.h"
#include "RaycastHelper.h"
#include "Weapon.h"
#include "EntityResource.h"
#include "EntityItem.h"
#include "DebugLog.h"
#include "EntityAsis.h"
#include "Player.h"
#include "SFXPoolManager.h"
#include "GameInstance.h"
#include "SceneTransitionUI.h"

void GameManager::Awake()
{
	LOG("GameManager Awake");
	//앞으론 언리얼 처럼 게임인스턴스를 활용해서 전역 설정값 관리
	GameInstance::GetInstance()->Initialize();

	auto resourcePool = GameObject::Find("ResourcePool");
	auto weaponPiecePool = GameObject::Find("WeaponPiecePool");

	if (resourcePool) {
		for (auto& index : resourcePool->m_childrenIndices) {
			auto object = GameObject::FindIndex(index);
			auto entity = object->GetComponent<EntityItem>();
			if (entity)
				m_resourcePool.push_back(entity);
		}
	}
	else {
		LOG("not assigned resourcePool");
	}
	if (weaponPiecePool) {
		for (auto& index : weaponPiecePool->m_childrenIndices) {
			auto object = GameObject::FindIndex(index);
			auto entity = object->GetComponent<Weapon>();
			if (entity)
				m_weaponPiecePool.push_back(entity);
		}
	}
	else {
		LOG("not assigned weaponPiecePool");
	}
}

void GameManager::Start()
{
	LOG("GameManager Start");

	auto transitionUIObj = GameObject::Find("SceneTransition");
	if (transitionUIObj) {
		m_sceneTransitionUI = transitionUIObj->GetComponent<SceneTransitionUI>();
		if (m_sceneTransitionUI)
		{
			m_sceneTransitionUI->FadeOut(0.3f);
		}
	}
	else {
		LOG("not assigned SceneTransitionUI");
	}
}

void GameManager::Update(float tick)
{
	GameInstance::GetInstance()->AsyncSceneLoadUpdate();

	//테스트용 보상 코드
	static float rewardTimer = 0.f;
	rewardTimer += tick;

	if (InputManagement->IsKeyPressed(KeyBoard::Escape))
	{
		SceneManagers->SetDecommissioning();
	}

	if (m_isGameOver)
	{
		int currentScene = GameInstance::GetInstance()->GetCurrentSceneType();
		if (currentScene != (int)SceneType::Boss)
		{
			GameInstance::GetInstance()->ResetAllEnhancements();
		}

		SwitchNextSceneWithFade();
		return;
	}

	if(rewardTimer >= 1.f)
	{
		rewardTimer = 0.f;
		//1초마다 보상
		if (m_isTestReward)
		{
			AddReward(99);
			//int reward = GetReward();
			//if (reward < 99)
			//{
			//	AddReward(1);
			//}
			//else
			//{
			//	InitReward(0);
			//}
			displayPollutionGaugeRatio += tick;
		}
		else
		{
			displayPollutionGaugeRatio = 0;
		}
	}

	int deadCount = 0;
	if (!m_isGameOver)
	{
		for (auto& player : m_players)
		{
			if (0 >= player->m_currentHP)
			{
				++deadCount;
			}
		}

		if (deadCount >= m_players.size() && m_players.size() > 0)
		{
			//모든 플레이어가 죽음
			int currentScene = GameInstance::GetInstance()->GetCurrentSceneType();
			if (currentScene != (int)SceneType::Stage && currentScene != (int)SceneType::Boss)
			{
				GameInstance::GetInstance()->SetCurrentSceneType((int)SceneType::Stage);
			}
			m_nextSceneIndex = (int)SceneType::GameOver;
			m_isLoadingReq = false;
			LoadNextScene();
			m_isGameOver = true;
		}
	}
}

void GameManager::OnDisable()
{
	LOG("GameManager OnDisable");
	for (auto& entity : m_entities)
	{
		auto meshrenderer = entity->GetOwner()->GetComponent<MeshRenderer>();
		if (meshrenderer)
		{
			meshrenderer->m_bitflag = 0;
		}
	}
}

void GameManager::LoadScene(int sceneType)
{
	GameInstance::GetInstance()->LoadSettingedScene(sceneType);
}

void GameManager::SwitchScene(int sceneType)
{
	GameInstance::GetInstance()->SwitchSettingedScene(sceneType);
}

void GameManager::SetPlayerInputDevice(int playerIndex, CharType charType, PlayerDir dir)
{
	if (playerIndex < 0 || playerIndex >= MAX_INPUT_DEVICE) return;

	GameInstance::GetInstance()->SetPlayerInputDevice(playerIndex, charType, dir);
	++selectPlayerCount;
}

void GameManager::RemovePlayerInputDevice(int playerIndex, CharType charType, PlayerDir dir)
{
	if (playerIndex < 0 || playerIndex >= MAX_INPUT_DEVICE) return;

	GameInstance::GetInstance()->RemovePlayerInputDevice(playerIndex, charType, dir);
	--selectPlayerCount;
}

float GameManager::GetAsisPollutionGaugeRatio()
{
	if(!m_isTestReward)
	{
		if (m_asis.empty()) return 0.f;
		auto asis = dynamic_cast<EntityAsis*>(m_asis[0]);
		if (!asis) return 0.f;
		return asis->GetPollutionGaugeRatio();
	}
	else
	{
		return displayPollutionGaugeRatio;
	}
}

void GameManager::LoadNextScene()
{
	if(m_isLoadingReq)
	{
		GameInstance::GetInstance()->SetAfterLoadSceneIndex(m_nextSceneIndex);
		LoadScene((int)SceneType::Loading);
	}
	else
	{
		LoadScene(m_nextSceneIndex);
	}
}

void GameManager::SwitchNextScene()
{
	if (!GameInstance::GetInstance()->IsLoadSceneComplete()) 
	{
		return;
	}

	if (m_isLoadingReq)
	{
		SwitchScene((int)SceneType::Loading);
	}
	else
	{
		SwitchScene(m_nextSceneIndex);
	}
}

void GameManager::LoadPrevScene()
{
	// 이전 씬의 경우 다음 씬과는 다르게 정보를 GameInstance에서 관리하고 있기 때문에 갱신이 필요함.
	m_prevSceneIndex = GameInstance::GetInstance()->GetPrevSceneType();
	if (m_isLoadingReq)
	{
		GameInstance::GetInstance()->SetAfterLoadSceneIndex(m_prevSceneIndex);
		LoadScene((int)SceneType::Loading);
	}
	else
	{
		LoadScene(m_prevSceneIndex);
	}
}

void GameManager::SwitchPrevScene()
{
	if (!GameInstance::GetInstance()->IsLoadSceneComplete())
	{
		return;
	}

	if (m_isLoadingReq)
	{
		SwitchScene((int)SceneType::Loading);
	}
	else
	{
		SwitchScene(m_prevSceneIndex);
	}
}

void GameManager::SwitchNextSceneWithFade()
{
	if (m_isSwitching) return;                    // 이미 진행 중
	if (!GameInstance::GetInstance()->IsLoadSceneComplete()) return;
	if (!m_sceneTransitionUI) { // 페이더 없으면 즉시 전환 (폴백)
		SwitchNextScene();
		return;
	}

	m_isSwitching = true;
	m_sceneTransitionUI->FadeIn(m_fadeInDuration, [this]()    // 페이드 인 완료 콜백
	{
		if (m_isLoadingReq) {
			SwitchScene(static_cast<int>(SceneType::Loading));
		}
		else {
			SwitchScene(m_nextSceneIndex);
		}

		// 새 씬에서 어둡게 시작했다면, 적절한 시점(로딩 완료 등)에 FadeOut 호출.
		// m_fader->FadeOut(0.5f, [this](){ m_isSwitching = false; });
		// 만약 FadeOut 시점이 여기 아니면, 전환 직후 바로 플래그 해제:
		m_isSwitching = false;
	});
}

void GameManager::SwitchPrevSceneWithFade()
{
	if (m_isSwitching) return;                    // 이미 진행 중
	if (!GameInstance::GetInstance()->IsLoadSceneComplete()) return;
	if (!m_sceneTransitionUI) { // 페이더 없으면 즉시 전환 (폴백)
		SwitchPrevScene();
		return;
	}
	m_isSwitching = true;
	m_sceneTransitionUI->FadeIn(m_fadeInDuration, [this]()    // 페이드 인 완료 콜백
	{
		if (m_isLoadingReq) 
		{
			SwitchScene(static_cast<int>(SceneType::Loading));
		}
		else 
		{
			SwitchScene(m_prevSceneIndex);
		}
		// 새 씬에서 어둡게 시작했다면, 적절한 시점(로딩 완료 등)에 FadeOut 호출.
		// m_fader->FadeOut(0.5f, [this](){ m_isSwitching = false; });
		// 만약 FadeOut 시점이 여기 아니면, 전환 직후 바로 플래그 해제:
		m_isSwitching = false;
	});
}

void GameManager::LoadImidiateNextScene()
{
}

void GameManager::ApplyGlobalEnhancementsToAllPlayers()
{
	// GameManager가 들고 있는 플레이어 목록에 순회 적용
	for (Entity* e : m_players) 
	{                             
		// GameManager가 보관하는 리스트 사용 :contentReference[oaicite:14]{index=14}
		if (auto* p = dynamic_cast<Player*>(e)) 
		{
			ApplyGlobalEnhancementsToPlayer(p);
		}
	}
}

void GameManager::ApplyGlobalEnhancementsToPlayer(Player* player)
{
	if (!player) return;

    // 최초 1회: 기준 스냅샷 확보(강화 누적값과 독립적인 '기준값')
    EnsureBaseSnapshot(player);

    auto& snap = m_baseByPlayer[player];
    auto* gi   = GameInstance::GetInstance();

    // 1) 이동속도 (float, Mul) : base * (1 + v)
    {
        const float finalMove = gi->ApplyToBaseFloat(ItemEnhancementType::MoveSpeedUp, snap.baseMoveSpeed);
        player->moveSpeed = finalMove;
    }

    // 2) 대시 횟수 (int, Add) : base + v
    {
        const int finalDash = gi->ApplyToBaseInt(ItemEnhancementType::DashCountUp, snap.baseDashAmount);
        player->dashAmount = finalDash;
    }

    // 3) 공격력 (int, Add) : base + v
    {
        const int finalAtk = gi->ApplyToBaseInt(ItemEnhancementType::Atk, snap.baseAtk);
        player->Atk = finalAtk;
    }

    // 4) 공격속도 배수 (float, Mul) : base * (1 + v)
    {
        const float finalAtkSpd = gi->ApplyToBaseFloat(ItemEnhancementType::AtkSpeedUp, snap.baseAtkSpeedMult);
        player->MultipleAttackSpeed = finalAtkSpd;
    }

    // 5) 최대 체력 (int, Add) : Entity::m_maxHP 기준으로 적용
    {
        Entity* e = static_cast<Entity*>(player);
        const int oldMax  = e->m_maxHP;
        const int finalMax = gi->ApplyToBaseInt(ItemEnhancementType::MaxHPUp, snap.baseMaxHP);
        e->m_maxHP = finalMax;

        // 현재 HP 보정 정책: 최대치 증가분만큼 현재 HP도 올림(초과 회복 방지)
        const int delta = finalMax - oldMax;
        if (delta > 0)
            e->m_currentHP = std::min(e->m_currentHP + delta, finalMax);
        else
            e->m_currentHP = std::min(e->m_currentHP, finalMax);
    }

    // (옵션) 6) 투척 거리 증가 (float, Add) : 필요 시 플레이어나 무기 시스템에 반영
    // 예: player->ThrowRange 가 있다면
     {
         const float finalThrowRange = gi->ApplyToBaseFloat(ItemEnhancementType::ThrowRangeUp, snap.baseThrowRange);
         player->detectDistance = finalThrowRange;
     }
}

void GameManager::PushEntity(Entity* entity)
{
	if (entity)
	{
		m_entities.push_back(entity);
	}
	else
	{
		LOG("Entity is null, cannot push to GameManager.");
	}
}

void GameManager::PushPlayer(Entity* player)
{
	m_players.push_back(player);
}

void GameManager::PushAsis(Entity* asis)
{
	m_asis.push_back(asis);
}

const std::vector<Entity*>& GameManager::GetEntities()
{
	return m_entities;
}

const std::vector<Entity*>& GameManager::GetPlayers()
{
	return m_players;
}

const std::vector<Entity*>& GameManager::GetAsis()
{
	return m_asis;
}

std::vector<Entity*>& GameManager::GetResourcePool()
{
	// TODO: 여기에 return 문을 삽입합니다.
	return m_resourcePool;
}

std::vector<Weapon*>& GameManager::GetWeaponPiecePool()
{
	// TODO: 여기에 return 문을 삽입합니다.
	return m_weaponPiecePool;
}

void GameManager::PushSFXPool(SFXPoolManager* _SFXPool)
{
	SFXPool = _SFXPool;
}

SFXPoolManager* GameManager::GetSFXPool()
{
	return SFXPool;
}

void GameManager::PushObjectPoolManager(ObjectPoolManager* _objPoolManager)
{
	objectPoolManager = _objPoolManager;
}

ObjectPoolManager* GameManager::GetObjectPoolManager()
{
	return objectPoolManager;
}

void GameManager::PushControllerVibration(ControllerVibration* _ControllerVibration)
{
	ControllerVibrationData = _ControllerVibration;
}

ControllerVibration* GameManager::GetControllerVibration()
{
	return ControllerVibrationData;
}

void GameManager::CheatMiningResource()
{
	auto cam = GameObject::Find("Main Camera");
	if (!cam) return;

	HitResult hit;
	Quaternion currentRotation = cam->m_transform.GetWorldQuaternion();
	currentRotation.Normalize();
	Vector3 currentForward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), currentRotation);

	bool size = Raycast(cam->m_transform.GetWorldPosition(), currentForward, 10.f, 1, hit);
	for (int i = 0; i < size; i++) {
		LOG(hit.gameObject->m_name.data());
	}

	/*for (auto& resource : m_resourcePool) {
		auto& rb = resource->GetComponent<RigidBodyComponent>();
		resource->GetOwner()->m_transform.SetScale(resource->GetOwner()->m_transform.GetWorldScale() * .3f);
		
		rb.AddForce((Mathf::Vector3::Up + Mathf::Vector3::Backward) * 300.f, EForceMode::IMPULSE);
	}*/
}

void GameManager::EnsureBaseSnapshot(Player* player)
{
	auto& snap = m_baseByPlayer[player];
	if (snap.initialized) return;

	// Player 쪽 기준값
	snap.baseMoveSpeed = player->baseMoveSpeed;          // 별도 기준 값 필드 존재 :contentReference[oaicite:7]{index=7}
	snap.baseDashAmount = player->dashAmount;             // 현재값을 초기 기준으로 스냅샷
	snap.baseAtk = player->Atk;
	snap.baseAtkSpeedMult = player->MultipleAttackSpeed;

	// Entity(부모) 쪽 최대체력 기준값
	Entity* e = static_cast<Entity*>(player);
	snap.baseMaxHP = e->m_maxHP; // Player의 maxHP 말고 Entity::m_maxHP 사용 :contentReference[oaicite:8]{index=8}
	snap.baseThrowRange = player->detectDistance;

	snap.initialized = true;
}

void GameManager::InitReward(int amount)
{
	GameInstance::GetInstance()->SetRewardAmount(amount);
}

void GameManager::AddReward(int amount)
{
	GameInstance::GetInstance()->AddRewardAmount(amount);
}

int GameManager::GetReward()
{
	return GameInstance::GetInstance()->GetRewardAmount();
}
