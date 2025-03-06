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

	float mass;				// 질량
	float drag;				// 저항력
	float angularDrag;		// 회전 저항력
	float centerOfMass[3];	// 무게중심 위치
	bool freezePosition[3];	// pos의 물리 결과를 반영하지 않음.
	bool freezeRotation[3];	// rot의 물리 결과를 반영하지 않음.
	bool useGravity;		// 중력 사용 여부
	bool isKinematic;		// 물리 결과에 대한 영향을 받을지 여부. (설정하면 물리로는 이동하지 않고 충돌은 감지 가능.)

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
	FORCE,				// 지속적인 힘. 질량에 의존
	IMPULSE,			// 순간적인 힘. 질량에 의존
	VELOCITY_CHANGE,	// 직접적인 속도 변경. 질량에 무관
	ACCELERATION		// 동일한 가속도 적용. 질량에 무관
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
	float radius = 0.4f;		// 캐릭터 반지름
	float height = 0.5f;		// 캐릭터 높이
	float stepOffset = 0.001f;	// 오를 수 있는 계단 높이 (코사인)
	float slopeLimit = 0.7f;	// 오를 수 있는 최대 경사각
	float contactOffset = 0.001f;	// 충돌 감지를 위한 여유 거리
	float maxJumpHeight = 1.0f;	// 최대 점프 높이
};

struct BoxShape {
	float worldPosition[3];
	float worldRotation[4];
	float halfSize[3];
};