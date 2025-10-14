#include "GameManager.h"
#include "pch.h"
#include "SceneManager.h"
#include "InputActionManager.h"
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

void GameManager::LoadScene(const std::string& sceneName)
{
	GameInstance::GetInstance()->LoadScene(sceneName);
}

void GameManager::SwitchScene(const std::string& sceneName)
{
	GameInstance::GetInstance()->SwitchScene(sceneName);
}

void GameManager::UnloadScene(const std::string& sceneName)
{
	GameInstance::GetInstance()->UnloadScene(sceneName);
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

void GameManager::LoadTestScene()
{
	LoadScene("CreateUIPrefabV2");
}

void GameManager::SwitchTestScene()
{
	SwitchScene("CreateUIPrefabV2");
}

void GameManager::LoadNextScene()
{
	if (m_nextSceneName.empty()) return;

	LoadScene(m_nextSceneName);
}

void GameManager::SwitchNextScene()
{
	if (m_nextSceneName.empty() || !GameInstance::GetInstance()->IsLoadSceneComplete()) 
	{
		return;
	}

	SwitchScene(m_nextSceneName);
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
	EnsureBaseSnapshot(player);

	auto& snap = m_baseByPlayer[player];
	auto* gi = GameInstance::GetInstance();

	// ---- 전역 강화치 조회 & 적용 ----
	// 1) 이동속도: 곱산(float)
	//    최종: baseMoveSpeed * (1 + MoveSpeedUp 누적 비율)
	{
		float finalMove = gi->ApplyToBaseFloat(ItemEnhancementType::MoveSpeedUp, snap.baseMoveSpeed);
		player->moveSpeed = finalMove;                       // Player::moveSpeed에 반영 :contentReference[oaicite:9]{index=9}
	}

	// 2) 대시 횟수: 가산(int)
	//    최종: baseDashAmount + DashCountUp 누적
	{
		int finalDash = gi->ApplyToBaseInt(ItemEnhancementType::DashCountUp, snap.baseDashAmount);
		player->dashAmount = finalDash;                      // Player::dashAmount 반영 :contentReference[oaicite:10]{index=10}
	}

	// 3) 공격력: 가산(int)
	//    최종: baseAtk + Atk 누적
	{
		int finalAtk = gi->ApplyToBaseInt(ItemEnhancementType::Atk, snap.baseAtk);
		player->Atk = finalAtk;                              // Player::Atk 반영 :contentReference[oaicite:11]{index=11}
	}

	// 4) 공격속도 배수: 곱산(float)
	//    최종: baseAtkSpeedMult * (1 + AtkSpeedUp 누적 비율)
	{
		float finalAtkSpd = gi->ApplyToBaseFloat(ItemEnhancementType::AtkSpeedUp, snap.baseAtkSpeedMult);
		player->MultipleAttackSpeed = finalAtkSpd;           // Player::MultipleAttackSpeed 반영 :contentReference[oaicite:12]{index=12}
	}

	// 5) 최대 체력: **Entity::m_maxHP** 기준으로 가산(int)
	//    최종: baseMaxHP + MaxHPUp 누적    (Player::maxHP 사용 금지!)
	{
		Entity* e = static_cast<Entity*>(player);
		int oldMax = e->m_maxHP;
		int finalMax = gi->ApplyToBaseInt(ItemEnhancementType::MaxHPUp, snap.baseMaxHP);
		e->m_maxHP = finalMax;                               // Entity::m_maxHP 반영 :contentReference[oaicite:13]{index=13}

		// 현재 HP 보정 정책(택1)
		// A) 비율 유지:
		// float ratio = (oldMax > 0) ? (float)e->m_currentHP / (float)oldMax : 1.f;
		// e->m_currentHP = (int)std::round(finalMax * ratio);

		// B) 최대치 증가분만큼 현재 HP도 올림(과잉 회복 방지):
		int delta = finalMax - oldMax;
		if (delta > 0) e->m_currentHP = std::min(e->m_currentHP + delta, finalMax);
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
	snap.baseMaxHP = e->m_maxHP;                             // Player의 maxHP 말고 Entity::m_maxHP 사용 :contentReference[oaicite:8]{index=8}

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
