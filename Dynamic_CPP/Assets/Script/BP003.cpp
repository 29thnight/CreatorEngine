#include "BP003.h"
#include "Entity.h"
#include "pch.h"
#include "PrefabUtility.h"
#include "Core.Random.h"
#include "TweenManager.h"
void BP003::Start()
{
}

void BP003::Update(float tick)
{
	if (!isInitialize || !isAttackStart) {
		return;
	}

	m_timer += tick;
	//���� ������ ���ϸ��̼� ȿ�� ��� �Ѵٰ� �ϸ� ����ٰ�

	//����� �˵� ���� ȿ��

	if (m_useOrbiting) {
		if (m_orbitingStartDelay < m_timer) {
			isOrbiting = true;
		}

		if ((m_delay - m_orbitingEndDelay) < m_timer) {
			isOrbiting = false;
		}
	}
	
	if (isOrbiting) 
	{

		Mathf::Vector3 ownerPos = m_ownerEntity->GetOwner()->GetComponent<Transform>()->GetWorldPosition();

		float orbitingSpeed = 1.0f; //ȸ�� �ӵ� 

		// 1. �� ������ �˵� ������ ������Ŵ
		float clockwise = m_clockWise ? 1.0f : -1.0f; //�ð���� �ݽð����
		m_orbitAngle += orbitingSpeed * clockwise * tick;

		// 2. �� ���� x, z ��ǥ ���
		float offsetX = cos(m_orbitAngle) * m_orbitDistance;
		float offsetZ = sin(m_orbitAngle) * m_orbitDistance;

		// 3. ��ǥ�� ��ġ�� offset�� ���� ���� ���ο� ��ġ�� ���
		
		Mathf::Vector3 newPosition = ownerPos + Mathf::Vector3(offsetX, 0, offsetZ);

		// 4. �� ��ġ�� ���� ����
		m_pOwner->GetComponent<Transform>()->SetWorldPosition(newPosition);

	}

	


	if (m_timer > m_delay) {
		Explosion(); 
	}

	if (ownerDestory) {
		GetOwner()->SetEnabled(false); //���� ������ �������
		GetOwner()->Destroy(); //���� ��ũ ����
	}
}

void BP003::Initialize(Entity* owner, Mathf::Vector3 pos, int damage, float radius, float delay, bool useOrbiting, bool clockwise)
{
	m_ownerEntity = owner;
	m_damage = damage;
	m_radius = radius;
	m_delay = delay;
	m_timer = 0.0f;

	//�ϴ� �ʱ�ȭ�� ����
	m_useOrbiting = useOrbiting;
	m_clockWise = clockwise;
	//��ü ȸ���� ���� ����
	//�켱 ���� ������ �Ÿ� ������ �ؾ� �ϴ� ��ȯ�� ���� �������� �Ÿ��� �����Ͽ� �������
	Mathf::Vector3 ownerPos = m_ownerEntity->GetOwner()->GetComponent<Transform>()->GetWorldPosition();
	GetOwner()->GetComponent<Transform>()->SetWorldPosition(pos);

	Mathf::Vector3 toMe = pos - ownerPos;
	toMe.y = 0;
	m_orbitAngle = atan2(toMe.z, toMe.x); //��ȯ�������� ����-->ȸ���� ������ ���� 
	m_orbitDistance = toMe.Length(); //��ȯ������ �Ÿ� -->ȸ���� ������ �Ÿ��� ������ ������ �ֵ���
	


	isInitialize = true;
}

void BP003::Explosion()
{
	//���� 
	//overlap ����
	Mathf::Vector3 pos = GetOwner()->GetComponent<Transform>()->GetWorldPosition();

	OverlapInput input;
	input.position = pos;
	input.rotation = Mathf::Quaternion::Identity;
	input.layerMask = 1 << 5; // �÷��̾� ���̾ �˻�

	std::vector<HitResult> res;

	//�̶� �߰� ����Ʈ�� �ε������� �ִ��� ������

	PhysicsManagers->SphereOverlap(input, m_radius, res);

	//���� ������ ������ ó��
	for (auto& hit : res) {
		//tag �Ǵ�? ���̾� �Ǵ�? �̸����� ã����? ���Ѵٸ� �ٲ㵵 ���� �ƴ� ���� �ʿ��ϴٸ� init�� ���̾� ����ũ���� �ѱ����
		//�÷��̾ ã�� �ƽý� �ȵ��´ٸ�
		
		Entity* objEntity = hit.gameObject->GetComponentDynamicCast<Entity>();
		objEntity->SendDamage(m_ownerEntity, m_damage);
	}

	//������ ��� �ʿ� ������ �ּ� ���߿� ������Ƽ ����������....
	Random<int> randcount(0,2);
	Random<int> randtype(0, 3);

	int count = randcount();
	if (count != 0) {
		for (size_t i = 0; i < count; i++)
		{
			int type = randtype();
			Prefab* itemPrefab = nullptr;
			switch (type)
			{
			case 0 :
				itemPrefab = PrefabUtilitys->LoadPrefab("BoxMushroom");
				break;
			case 1 :
				itemPrefab = PrefabUtilitys->LoadPrefab("BoxMineral");
				break;
			case 2 : 
				itemPrefab = PrefabUtilitys->LoadPrefab("BoxFruit");
				break;
			case 3 :
				itemPrefab = PrefabUtilitys->LoadPrefab("BoxFlower");
				break;
			default:
				break;
			}

			if (itemPrefab)
			{
				GameObject* itemObj = PrefabUtilitys->InstantiatePrefab(itemPrefab, "entityItem");
				Mathf::Vector3 spawnPos = GetOwner()->m_transform.GetWorldPosition();
				spawnPos.y += 0.1f;
				itemObj->m_transform.SetPosition(spawnPos);

				Random<float> randX(-3.0f, 3.0f);
				Random<float> randY(0.2f, 1.f);
				Random<float> randZ(-3.0f, 3.0f);

				float randx = randX();
				float randy = randY();
				float randz = randZ();

				Mathf::Vector3 temp = { randx, randy,randz };
				float f = Random<float>(2.f, 3.f).Generate();
				auto tween = std::make_shared<Tweener<float>>(
					[=]() { return 0.f; },
					[=](float val) {
						Mathf::Vector3 pos = spawnPos;
						float force = f; // �߷� ����ϰ� y�� �
						pos.x = Mathf::Lerp(spawnPos.x, spawnPos.x + temp.x, val);
						pos.z = Mathf::Lerp(spawnPos.z, spawnPos.z + temp.z, val);
						pos.y = Mathf::Lerp(spawnPos.y, spawnPos.y + temp.y, val)
							+ force * (1 - (2 * val - 1) * (2 * val - 1));
						itemObj->m_transform.SetPosition(pos);
					},
					1.f,
					.5f,
					[](float t) { return Easing::Linear(t); }
				);


				auto GM = GameObject::Find("GameManager");
				if (GM)
				{
					auto tweenManager = GM->GetComponent<TweenManager>();
					if (tweenManager)
					{
						tweenManager->AddTween(tween);
					}
				}
			}
		}
	}

	m_timer = 0.0f;
	GetOwner()->SetEnabled(false);

}

