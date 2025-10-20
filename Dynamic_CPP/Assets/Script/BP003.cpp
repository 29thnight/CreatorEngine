#include "BP003.h"
#include "Entity.h"
#include "pch.h"
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

	//

	m_timer = 0.0f;
	GetOwner()->SetEnabled(false);

}

