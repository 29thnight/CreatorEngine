#pragma once

enum PhysFilter {
	WORD0 = 0x00000000, // 0
	WORD1 = 0x00000001, // 1
	WORD2 = 0x00000002, // 2
	WORD3 = 0x00000004, // 4
	WORD4 = 0x00000008, // 8
	WORD5 = 0x00000010, // 16
	WORD6 = 0x00000020, // 32
	WORD7 = 0x00000040, // 64
	WORD8 = 0x00000080, // 128
	WORDALL = 0x000000FF // 255
};

struct RigidbodyInfo
{
	RigidbodyInfo() :
		mass(1.f),
		drag(0.f),
		angularDrag(0.05f),
		centerOfMass{ 0.f, 0.f, 0.f },
		freezePosition{ false, false, false },
		freezeRotation{ false, false, false },
		useGravity(false),
		isKinematic(true),
		isCharacterController(false) {
	}

	bool changeflag = false;

	float mass;				// ����
	float drag;				// ���׷�
	float angularDrag;		// ȸ�� ���׷�
	float centerOfMass[3];	// �����߽� ��ġ
	bool freezePosition[3];	// pos�� ���� ����� �ݿ����� ����.
	bool freezeRotation[3];	// rot�� ���� ����� �ݿ����� ����.
	bool useGravity;		// �߷� ��� ����
	bool isKinematic;		// ���� ����� ���� ������ ������ ����. (�����ϸ� �����δ� �̵����� �ʰ� �浹�� ���� ����.)

	float prePosition[3] = { 0.f, 0.f, 0.f };
	float preRotation[4] = { 0.f, 0.f, 0.f, 1.f};

	bool isCharacterController;
};

struct ColliderInfo {
	ColliderInfo() :
		enable(true), isTrigger(false) {
	}
	
	bool changeflag = false;

	bool enable;
	bool isTrigger;
	float localPosition[3] = { 0.f, 0.f, 0.f };
	float localRotation[3] = { 0.f, 0.f, 0.f };
	int flag = PhysFilter::WORDALL;
	int simulFlag = 0;
};


enum ForceMode {
	FORCE,				// �������� ��. ������ ����
	IMPULSE,			// �������� ��. ������ ����
	VELOCITY_CHANGE,	// �������� �ӵ� ����. ������ ����
	ACCELERATION		// ������ ���ӵ� ����. ������ ����
};

enum Geometry {
	SPHERE,
	PLANE,
	CAPSULE,
	BOX,
	CONVEXCORE,
	CONVEXMESH,
	PARTICLESYSTEM,
	TETRAHEDRONMESH,
	TRIANGLEMESH,
	HEIGHTFIELD,
	CUSTOM,

	GEOMETRY_COUNT,		//!< internal use only!
	INVALID = -1		//!< internal use only!
};

struct HitInfo {
	void* iCollider;
	void* iRigidbody;
	float hitPosition[3];
	float hitNormal[3];
};

namespace physx {
	struct Plane {
		float normal[3] = { 0.f, 1.f, 0.f };
		float dist = 0.f;
	};
}

struct CharacterControllerAttribute {
	float radius = 0.4f;		// ĳ���� ������
	float height = 0.5f;		// ĳ���� ����
	float stepOffset = 0.001f;	// ���� �� �ִ� ��� ���� (�ڻ���)
	float slopeLimit = 0.7f;	// ���� �� �ִ� �ִ� ��簢
	float contactOffset = 0.001f;	// �浹 ������ ���� ���� �Ÿ�
	float maxJumpHeight = 1.0f;	// �ִ� ���� ����
};

struct BoxShape {
	float worldPosition[3];
	float worldRotation[4];
	float halfSize[3];
};