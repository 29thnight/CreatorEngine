#pragma once
//아 몰랑 4Quest 따라해보장
#include <directxtk/SimpleMath.h>
#include <string>
#include <vector>
#include <array>

//type id 
const static uint32_t m_boxTypeId = 9001;
const static uint32_t m_sphereTypeId = 9002;
const static uint32_t m_capsuleTypeId = 9003;
const static uint32_t m_planeTypeId = 9004;
const static uint32_t m_heightFieldTypeId = 9005;
const static uint32_t m_controllerTypeId = 9006;






//==========================================================
// EnumClass
//콜라이더 타입 //트리거 : 충돌되지 않고 겹치면 이벤트 발생 , 콜리전 : 충돌되면 이벤트 발생
enum class EColliderType {
	TRIGGER,
	COLLISION
};

//콜백 함수에 전달할 이벤트 타입
enum class ECollisionEventType {
	ENTER_OVERLAP = 0 ,
	ON_OVERLAP,
	END_OVERLAP,
	ENTER_COLLISION,
	ON_COLLISION,
	END_COLLISION,
};

//관절의 돌림	축
enum class EArticulationAxis {
	X_AXIS,
	Y_AXIS,
	Z_AXIS,
};

//관절의 모션 제한
enum class EArticulationMotion {
	LOCKED = 0,
	LIMITED = 1,
	FREE = 2,

	END
};


//케릭터 컨트롤러 이동제한
enum class ERestrictDirection {
	PlUS_X,
	MINUS_X,
	PLUS_Z,
	MINUS_Z,
	END
};

//==========================================================
struct PhysicsTransform
{
	DirectX::SimpleMath::Vector3	localPosition;
	DirectX::SimpleMath::Quaternion localRotation;
	DirectX::SimpleMath::Vector3	localScale;

	DirectX::SimpleMath::Vector3	worldPosition;
	DirectX::SimpleMath::Quaternion worldRotation;
	DirectX::SimpleMath::Vector3	worldScale;

	DirectX::SimpleMath::Matrix localMatrix;
	DirectX::SimpleMath::Matrix worldMatrix;
	//Transform* parent;
};

//GetSetData구조체
//RigidBody의 GetSetData를 위한 구조체
struct RigidBodyGetSetData
{
	DirectX::SimpleMath::Matrix  transform = {};	//트렌스폼
	DirectX::SimpleMath::Vector3 linearVelocity = {};	//직선 방향 속도 
	DirectX::SimpleMath::Vector3 angularVelocity = {};	//회전 방향 속도
	unsigned int LayerNumber = UINT_MAX;	//레이어 넘버

	bool isLockLinearX = false;	//X축 고정
	bool isLockLinearY = false;	//Y축 고정
	bool isLockLinearZ = false;	//Z축 고정
	bool isLockAngularX = false;	//X축 회전 고정
	bool isLockAngularY = false;	//Y축 회전 고정
	bool isLockAngularZ = false;	//Z축 회전 고정
};

//CharacterController의 GetSetData를 위한 구조체
struct CharacterControllerGetSetData
{
	DirectX::SimpleMath::Vector3 position = {};	//위치
	DirectX::SimpleMath::Quaternion rotation = {};	//회전
	DirectX::SimpleMath::Vector3 Scale = { 1.f, 1.f, 1.f };	//스케일
	unsigned int LayerNumber = UINT_MAX;	//레이어 넘버
};

//CharacterMovement의 GetSetData를 위한 구조체
struct CharacterMovementGetSetData
{
	DirectX::SimpleMath::Vector3 velocity = { };   //캐릭터 컨트롤러의 속도
	float maxSpeed;	//최대 속도
	float acceleration;	//가속도
	bool isFall = false;	//낙하중인지 
	std::array<bool, 4> restrictDirection;	//이동 제한 방향 설정
};

//==========================================================
//관절 구조체 getter , setter

// joint 의 로컬 트렌스폼을 설정하기 위한 구조체
struct ArticulationLinkGetData {
	std::string name; //관절 이름
	DirectX::SimpleMath::Matrix jointLocalTransform = {}; //관절 로컬 트렌스폼
};

// joint를 적용할 본의 월드 트렌스폼을 설정하기 위한 구조체
struct ArticulationLinkSetData {
	std::string name; //관절 이름
	DirectX::SimpleMath::Matrix boneWorldTransform = {}; //적용될 본의 월드 트렌스폼
};

struct ArticulationGetData {  
	DirectX::SimpleMath::Matrix WorldTransform = DirectX::SimpleMath::Matrix(); // 초기화 문제 수정  
	std::vector<ArticulationLinkGetData> linkData; // joint의 모든 링크 데이터  
	bool bIsRagdollSimulation = false; // ragdoll 시뮬레이션 여부  
	unsigned int LayerNumber = UINT_MAX; // 레이어 넘버  
}; // E0065 오류 수정: 세미콜론 추가

struct ArticulationSetData {
	DirectX::SimpleMath::Matrix WorldTransform = DirectX::SimpleMath::Matrix();
	std::vector<ArticulationLinkSetData> linkData; //joint의 모든 링크 데이터	
	bool bIsRagdollSimulation = false; // ragdoll 시뮬레이션 여부
	unsigned int LayerNumber = UINT_MAX; //레이어 넘버
};

//==========================================================
//Resource
struct ConvexMeshResourceInfo
{
	DirectX::SimpleMath::Vector3* vertices = nullptr;	//모데 버텍스 정보
	int vertexSize = 0;	//모델 버텍스 사이즈
	unsigned char convexPolygonLimit = 4; 	//Convex 폴리곤의 최대 갯수 최소 4개 , 최대 256개
};

struct MaterialResourceInfo
{
	float staticFriction = 1.0f;	//정적 물체 마찰 계수
	float dynamicFriction = 1.0f;	//동적 물체 마찰 계수
	float restitution = 1.0f;	//탄성 계수
	float density = 1.0f;	//밀도
};

//==========================================================
//ColliderInfo
const unsigned int unresiterID = 0; //등록되지 않은 ID

//콜라이더 물리 충돌 처리 형태 정보
struct ColliderInfo
{
	unsigned int id = unresiterID;	//콜라이더 ID
	unsigned int layerNumber = 0;	//레이어 넘버
	PhysicsTransform collsionTransform;	//콜리전 트렌스폼
	float staticFriction = 1.0f;	//정적 물체 마찰 계수
	float dynamicFriction = 1.0f;	//동적 물체 마찰 계수
	float restitution = 1.0f;	//탄성 계수
	float density = 1.0f;	//밀도
};

//구형 콜라이더 정보
struct SphereColliderInfo 
{
	ColliderInfo colliderInfo;	//콜라이더 정보
	float radius = 1.0f;	//구 반지름
};

//박스형 콜라이더 정보
struct BoxColliderInfo
{
	ColliderInfo colliderInfo;	//콜라이더 정보
	DirectX::SimpleMath::Vector3 boxExtent = {};	//박스 반지름
};

//캡슐형 콜라이더 정보
struct CapsuleColliderInfo
{
	ColliderInfo colliderInfo;	//콜라이더 정보
	float radius = 1.0f;	//캡슐 반지름
	float height = 1.0f;	//캡슐 높이
};

//메쉬 정보로 만드는 Convex 콜라이더 정보
struct ConvexMeshColliderInfo
{
	ColliderInfo colliderInfo;	//콜라이더 정보
	DirectX::SimpleMath::Vector3* vertices = nullptr;	//모델 버텍스 정보
	int vertexSize = 0;	//모델 버텍스 사이즈
	unsigned char convexPolygonLimit = 4; 	//Convex 폴리곤의 최대 갯수 최소 4개 , 최대 256개
	//unsigned int convexMeshHash = 0;	//ConvexMesh 해쉬값
};

//메쉬 정보로 만드는 평면형 콜라이더 정보 //-> physx::PxDeformableSurface 로 대체 할듯 
struct TriangleMeshColliderInfo
{
	ColliderInfo colliderInfo;	//콜라이더 정보
	//unsigned int triangleMeshHash = 0;	//TriangleMesh 해쉬값
	DirectX::SimpleMath::Vector3* vertices = nullptr;	//모델 버텍스 정보
	int vertexSize = 0;	//모델 버텍스 사이즈
	unsigned int* indices = nullptr;	//모델 인덱스 정보
	int indexSize = 0;	//모델 인덱스 사이즈
};

//hegihtfield 정보로 만드는 terrain collider 정보
struct HeightFieldColliderInfo
{
	ColliderInfo colliderInfo;	//콜라이더 정보
	//unsigned int heightFieldHash = 0;	//HeightField 해쉬값
	int* heightMep = nullptr;	//모델 높이 맵 정보
	unsigned int numCols = 0;	//높이 맵 가로 겟수
	unsigned int numRows = 0;	//높이 맵 세로 겟수
	float rowScale = 1.0f;	//높이 맵 세로 유닛 길이
	float colScale = 1.0f;	//높이 맵 가로 유닛 길이
	float heightScale = 1.0f;	//높이 맵 높이 유닛 길이
};
//==========================================================
//characterController

//캐릭터 이동에 대한 정보
struct CharacterMovementInfo {
	float maxSpeed = 0.025f;	//최대 속도
	float acceleration = 1.0f;	//가속도
	float staticFriction = 0.4f;	//정적 물체 마찰 계수
	float dynamicFriction = 0.1f;	//동적 물체 마찰 계수
	float jumpSpeed = 0.05f;	//점프 속도
	float jumpXZAcceleration = 0.1f;	//점프 시 XZ 방향 가속도
	float jumpXZDeceleration = 0.1f;	//점프 시 XZ 방향 감속도
	float gravityWeight = 0.2f;	//중력 가속도
};

//캐릭터 컨트롤러 정보
struct CharacterControllerInfo
{
	unsigned int id = unresiterID;	//캐릭터 컨트롤러 ID
	unsigned int layerNumber = 0;	//레이어 넘버

	DirectX::SimpleMath::Vector3 position = {0.0f,0.0f,0.0f};	//위치
	float height = 0.1f;	//높이
	float radius = 0.05f;	//반지름
	float stepOffset = 0.001f;	//오를 수 있는 계단 높이 (코사인)
	float slopeLimit = 0.7f;	//오를 수 있는 최대 경사각
	float contactOffset = 0.001f;	//충돌 감지를 위한 여유 거리
};

struct CharactorControllerInputInfo {
	unsigned int id;	//캐릭터 컨트롤러 ID
	DirectX::SimpleMath::Vector3 input;	//이동 방향
	bool isDynamic;	//다이나믹인지
};

//==========================================================
//관절 정보
struct JointAxisInfo {
	EArticulationMotion motion = EArticulationMotion::LIMITED;	//관절 모션 
	float limitlow = -60.0f;	//관절 회전 최소 제한 각도
	float limitHigh = 60.0f;	//관절 회전 최대 제한 각도
};

struct JointInfo {
	JointAxisInfo xAxisInfo;	//관절 축 정보
	JointAxisInfo yAxisInfo;	//관절 축 정보
	JointAxisInfo zAxisInfo;	//관절 축 정보
	DirectX::SimpleMath::Matrix localTransform = DirectX::SimpleMath::Matrix();	//관절 로컬 트렌스폼
	DirectX::SimpleMath::Matrix worldTransform = DirectX::SimpleMath::Matrix();	//관절 월드 트렌스폼
	float stiffness = 0.0f;	//관절 강도
	float damping = 1.0f;	//관절 감쇠
	float maxForce = 1.0f;	//관절 최대 힘
};

struct LinkInfo {
	std::string boneName;	//본 이름
	std::string parentBoneName;	//부모 본 이름
	float density = 1.0f;	//밀도
	DirectX::SimpleMath::Matrix localTransform = DirectX::SimpleMath::Matrix();	//joint 로컬 트렌스폼
	DirectX::SimpleMath::Matrix worldTransform = DirectX::SimpleMath::Matrix();	//joint 월드 트렌스폼
	DirectX::SimpleMath::Matrix boneWorldTransform = DirectX::SimpleMath::Matrix();	//본 월드 트렌스폼
	DirectX::SimpleMath::Matrix rootWorldTransform = DirectX::SimpleMath::Matrix();	//model root 월드 트렌스폼
	JointInfo jointInfo;	//joint 정보
};

struct ArticulationInfo {
	unsigned int id = unresiterID;	//관절 ID
	unsigned int layerNumber = 0;	//레이어 넘버
	DirectX::SimpleMath::Matrix worldTransform = DirectX::SimpleMath::Matrix();	//관절 월드 트렌스폼
	float staticFriction = 1.0f;	//정적 물체 마찰 계수
	float dynamicFriction = 1.0f;	//동적 물체 마찰 계수
	float restitution = 1.0f;	//탄성 계수
	float density = 1.0f;	//밀도
};

//==========================================================
//충돌
//충돌 정보
struct CollisionData
{
	unsigned int thisId = 0;
	unsigned int otherId = 0;
	unsigned int thisLayerNumber = 0;
	unsigned int otherLayerNumber = 0;
	std::vector<DirectX::SimpleMath::Vector3> contactPoints = {};	//충돌 지점
	bool isDead = false;	//충돌이 끝났는지
};

//충돌체크를 위한 RayCast
// 입력
struct RayCastInput
{
	unsigned int layerNumber;	//체크할 레이어 넘버
	DirectX::SimpleMath::Vector3 origin;	//시작점
	DirectX::SimpleMath::Vector3 direction;	//방향
	float distance;	//거리
};

// 출력
struct RayCastOutput
{
	//block이 발생한 경우
	bool hasBlock = false;	//block이 발생했는지
	unsigned int id = -1;	//block된 콜라이더 ID
	DirectX::SimpleMath::Vector3 blockPosition = {};	//block된 지점

	//hit가 발생한 경우
	unsigned int hitSize = 0;	//hit된 콜라이더 갯수
	std::vector<unsigned int> hitId;	//hit된 콜라이더 ID
	std::vector<unsigned int> hitLayerNumber;	//hit된 콜라이더 레이어 넘버
	std::vector<DirectX::SimpleMath::Vector3> contectPoints;	//각 hit된 콜라이더의 접촉 지점
};
//==========================================================
//todo : PxDeformableSuface 에 관한 정보