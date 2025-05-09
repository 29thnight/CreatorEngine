#pragma once
//�� ���� 4Quest �����غ���
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
//�ݶ��̴� Ÿ�� //Ʈ���� : �浹���� �ʰ� ��ġ�� �̺�Ʈ �߻� , �ݸ��� : �浹�Ǹ� �̺�Ʈ �߻�
enum class EColliderType {
	TRIGGER,
	COLLISION
};

//�ݹ� �Լ��� ������ �̺�Ʈ Ÿ��
enum class ECollisionEventType {
	ENTER_OVERLAP = 0 ,
	ON_OVERLAP,
	END_OVERLAP,
	ENTER_COLLISION,
	ON_COLLISION,
	END_COLLISION,
};

//������ ����	��
enum class EArticulationAxis {
	X_AXIS,
	Y_AXIS,
	Z_AXIS,
};

//������ ��� ����
enum class EArticulationMotion {
	LOCKED = 0,
	LIMITED = 1,
	FREE = 2,

	END
};


//�ɸ��� ��Ʈ�ѷ� �̵�����
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

//GetSetData����ü
//RigidBody�� GetSetData�� ���� ����ü
struct RigidBodyGetSetData
{
	DirectX::SimpleMath::Matrix  transform = {};	//Ʈ������
	DirectX::SimpleMath::Vector3 linearVelocity = {};	//���� ���� �ӵ� 
	DirectX::SimpleMath::Vector3 angularVelocity = {};	//ȸ�� ���� �ӵ�
	unsigned int LayerNumber = UINT_MAX;	//���̾� �ѹ�

	bool isLockLinearX = false;	//X�� ����
	bool isLockLinearY = false;	//Y�� ����
	bool isLockLinearZ = false;	//Z�� ����
	bool isLockAngularX = false;	//X�� ȸ�� ����
	bool isLockAngularY = false;	//Y�� ȸ�� ����
	bool isLockAngularZ = false;	//Z�� ȸ�� ����
};

//CharacterController�� GetSetData�� ���� ����ü
struct CharacterControllerGetSetData
{
	DirectX::SimpleMath::Vector3 position = {};	//��ġ
	DirectX::SimpleMath::Quaternion rotation = {};	//ȸ��
	DirectX::SimpleMath::Vector3 Scale = { 1.f, 1.f, 1.f };	//������
	unsigned int LayerNumber = UINT_MAX;	//���̾� �ѹ�
};

//CharacterMovement�� GetSetData�� ���� ����ü
struct CharacterMovementGetSetData
{
	DirectX::SimpleMath::Vector3 velocity = { };   //ĳ���� ��Ʈ�ѷ��� �ӵ�
	float maxSpeed;	//�ִ� �ӵ�
	float acceleration;	//���ӵ�
	bool isFall = false;	//���������� 
	std::array<bool, 4> restrictDirection;	//�̵� ���� ���� ����
};

//==========================================================
//���� ����ü getter , setter

// joint �� ���� Ʈ�������� �����ϱ� ���� ����ü
struct ArticulationLinkGetData {
	std::string name; //���� �̸�
	DirectX::SimpleMath::Matrix jointLocalTransform = {}; //���� ���� Ʈ������
};

// joint�� ������ ���� ���� Ʈ�������� �����ϱ� ���� ����ü
struct ArticulationLinkSetData {
	std::string name; //���� �̸�
	DirectX::SimpleMath::Matrix boneWorldTransform = {}; //����� ���� ���� Ʈ������
};

struct ArticulationGetData {  
	DirectX::SimpleMath::Matrix WorldTransform = DirectX::SimpleMath::Matrix(); // �ʱ�ȭ ���� ����  
	std::vector<ArticulationLinkGetData> linkData; // joint�� ��� ��ũ ������  
	bool bIsRagdollSimulation = false; // ragdoll �ùķ��̼� ����  
	unsigned int LayerNumber = UINT_MAX; // ���̾� �ѹ�  
}; // E0065 ���� ����: �����ݷ� �߰�

struct ArticulationSetData {
	DirectX::SimpleMath::Matrix WorldTransform = DirectX::SimpleMath::Matrix();
	std::vector<ArticulationLinkSetData> linkData; //joint�� ��� ��ũ ������	
	bool bIsRagdollSimulation = false; // ragdoll �ùķ��̼� ����
	unsigned int LayerNumber = UINT_MAX; //���̾� �ѹ�
};

//==========================================================
//Resource
struct ConvexMeshResourceInfo
{
	DirectX::SimpleMath::Vector3* vertices = nullptr;	//�� ���ؽ� ����
	int vertexSize = 0;	//�� ���ؽ� ������
	unsigned char convexPolygonLimit = 4; 	//Convex �������� �ִ� ���� �ּ� 4�� , �ִ� 256��
};

struct MaterialResourceInfo
{
	float staticFriction = 1.0f;	//���� ��ü ���� ���
	float dynamicFriction = 1.0f;	//���� ��ü ���� ���
	float restitution = 1.0f;	//ź�� ���
	float density = 1.0f;	//�е�
};

//==========================================================
//ColliderInfo
const unsigned int unresiterID = 0; //��ϵ��� ���� ID

//�ݶ��̴� ���� �浹 ó�� ���� ����
struct ColliderInfo
{
	unsigned int id = unresiterID;	//�ݶ��̴� ID
	unsigned int layerNumber = 0;	//���̾� �ѹ�
	PhysicsTransform collsionTransform;	//�ݸ��� Ʈ������
	float staticFriction = 1.0f;	//���� ��ü ���� ���
	float dynamicFriction = 1.0f;	//���� ��ü ���� ���
	float restitution = 1.0f;	//ź�� ���
	float density = 1.0f;	//�е�
};

//���� �ݶ��̴� ����
struct SphereColliderInfo 
{
	ColliderInfo colliderInfo;	//�ݶ��̴� ����
	float radius = 1.0f;	//�� ������
};

//�ڽ��� �ݶ��̴� ����
struct BoxColliderInfo
{
	ColliderInfo colliderInfo;	//�ݶ��̴� ����
	DirectX::SimpleMath::Vector3 boxExtent = {};	//�ڽ� ������
};

//ĸ���� �ݶ��̴� ����
struct CapsuleColliderInfo
{
	ColliderInfo colliderInfo;	//�ݶ��̴� ����
	float radius = 1.0f;	//ĸ�� ������
	float height = 1.0f;	//ĸ�� ����
};

//�޽� ������ ����� Convex �ݶ��̴� ����
struct ConvexMeshColliderInfo
{
	ColliderInfo colliderInfo;	//�ݶ��̴� ����
	DirectX::SimpleMath::Vector3* vertices = nullptr;	//�� ���ؽ� ����
	int vertexSize = 0;	//�� ���ؽ� ������
	unsigned char convexPolygonLimit = 4; 	//Convex �������� �ִ� ���� �ּ� 4�� , �ִ� 256��
	//unsigned int convexMeshHash = 0;	//ConvexMesh �ؽ���
};

//�޽� ������ ����� ����� �ݶ��̴� ���� //-> physx::PxDeformableSurface �� ��ü �ҵ� 
struct TriangleMeshColliderInfo
{
	ColliderInfo colliderInfo;	//�ݶ��̴� ����
	//unsigned int triangleMeshHash = 0;	//TriangleMesh �ؽ���
	DirectX::SimpleMath::Vector3* vertices = nullptr;	//�� ���ؽ� ����
	int vertexSize = 0;	//�� ���ؽ� ������
	unsigned int* indices = nullptr;	//�� �ε��� ����
	int indexSize = 0;	//�� �ε��� ������
};

//hegihtfield ������ ����� terrain collider ����
struct HeightFieldColliderInfo
{
	ColliderInfo colliderInfo;	//�ݶ��̴� ����
	//unsigned int heightFieldHash = 0;	//HeightField �ؽ���
	int* heightMep = nullptr;	//�� ���� �� ����
	unsigned int numCols = 0;	//���� �� ���� �ټ�
	unsigned int numRows = 0;	//���� �� ���� �ټ�
	float rowScale = 1.0f;	//���� �� ���� ���� ����
	float colScale = 1.0f;	//���� �� ���� ���� ����
	float heightScale = 1.0f;	//���� �� ���� ���� ����
};
//==========================================================
//characterController

//ĳ���� �̵��� ���� ����
struct CharacterMovementInfo {
	float maxSpeed = 0.025f;	//�ִ� �ӵ�
	float acceleration = 1.0f;	//���ӵ�
	float staticFriction = 0.4f;	//���� ��ü ���� ���
	float dynamicFriction = 0.1f;	//���� ��ü ���� ���
	float jumpSpeed = 0.05f;	//���� �ӵ�
	float jumpXZAcceleration = 0.1f;	//���� �� XZ ���� ���ӵ�
	float jumpXZDeceleration = 0.1f;	//���� �� XZ ���� ���ӵ�
	float gravityWeight = 0.2f;	//�߷� ���ӵ�
};

//ĳ���� ��Ʈ�ѷ� ����
struct CharacterControllerInfo
{
	unsigned int id = unresiterID;	//ĳ���� ��Ʈ�ѷ� ID
	unsigned int layerNumber = 0;	//���̾� �ѹ�

	DirectX::SimpleMath::Vector3 position = {0.0f,0.0f,0.0f};	//��ġ
	float height = 0.1f;	//����
	float radius = 0.05f;	//������
	float stepOffset = 0.001f;	//���� �� �ִ� ��� ���� (�ڻ���)
	float slopeLimit = 0.7f;	//���� �� �ִ� �ִ� ��簢
	float contactOffset = 0.001f;	//�浹 ������ ���� ���� �Ÿ�
};

struct CharactorControllerInputInfo {
	unsigned int id;	//ĳ���� ��Ʈ�ѷ� ID
	DirectX::SimpleMath::Vector3 input;	//�̵� ����
	bool isDynamic;	//���̳�������
};

//==========================================================
//���� ����
struct JointAxisInfo {
	EArticulationMotion motion = EArticulationMotion::LIMITED;	//���� ��� 
	float limitlow = -60.0f;	//���� ȸ�� �ּ� ���� ����
	float limitHigh = 60.0f;	//���� ȸ�� �ִ� ���� ����
};

struct JointInfo {
	JointAxisInfo xAxisInfo;	//���� �� ����
	JointAxisInfo yAxisInfo;	//���� �� ����
	JointAxisInfo zAxisInfo;	//���� �� ����
	DirectX::SimpleMath::Matrix localTransform = DirectX::SimpleMath::Matrix();	//���� ���� Ʈ������
	DirectX::SimpleMath::Matrix worldTransform = DirectX::SimpleMath::Matrix();	//���� ���� Ʈ������
	float stiffness = 0.0f;	//���� ����
	float damping = 1.0f;	//���� ����
	float maxForce = 1.0f;	//���� �ִ� ��
};

struct LinkInfo {
	std::string boneName;	//�� �̸�
	std::string parentBoneName;	//�θ� �� �̸�
	float density = 1.0f;	//�е�
	DirectX::SimpleMath::Matrix localTransform = DirectX::SimpleMath::Matrix();	//joint ���� Ʈ������
	DirectX::SimpleMath::Matrix worldTransform = DirectX::SimpleMath::Matrix();	//joint ���� Ʈ������
	DirectX::SimpleMath::Matrix boneWorldTransform = DirectX::SimpleMath::Matrix();	//�� ���� Ʈ������
	DirectX::SimpleMath::Matrix rootWorldTransform = DirectX::SimpleMath::Matrix();	//model root ���� Ʈ������
	JointInfo jointInfo;	//joint ����
};

struct ArticulationInfo {
	unsigned int id = unresiterID;	//���� ID
	unsigned int layerNumber = 0;	//���̾� �ѹ�
	DirectX::SimpleMath::Matrix worldTransform = DirectX::SimpleMath::Matrix();	//���� ���� Ʈ������
	float staticFriction = 1.0f;	//���� ��ü ���� ���
	float dynamicFriction = 1.0f;	//���� ��ü ���� ���
	float restitution = 1.0f;	//ź�� ���
	float density = 1.0f;	//�е�
};

//==========================================================
//�浹
//�浹 ����
struct CollisionData
{
	unsigned int thisId = 0;
	unsigned int otherId = 0;
	unsigned int thisLayerNumber = 0;
	unsigned int otherLayerNumber = 0;
	std::vector<DirectX::SimpleMath::Vector3> contactPoints = {};	//�浹 ����
	bool isDead = false;	//�浹�� ��������
};

//�浹üũ�� ���� RayCast
// �Է�
struct RayCastInput
{
	unsigned int layerNumber;	//üũ�� ���̾� �ѹ�
	DirectX::SimpleMath::Vector3 origin;	//������
	DirectX::SimpleMath::Vector3 direction;	//����
	float distance;	//�Ÿ�
};

// ���
struct RayCastOutput
{
	//block�� �߻��� ���
	bool hasBlock = false;	//block�� �߻��ߴ���
	unsigned int id = -1;	//block�� �ݶ��̴� ID
	DirectX::SimpleMath::Vector3 blockPosition = {};	//block�� ����

	//hit�� �߻��� ���
	unsigned int hitSize = 0;	//hit�� �ݶ��̴� ����
	std::vector<unsigned int> hitId;	//hit�� �ݶ��̴� ID
	std::vector<unsigned int> hitLayerNumber;	//hit�� �ݶ��̴� ���̾� �ѹ�
	std::vector<DirectX::SimpleMath::Vector3> contectPoints;	//�� hit�� �ݶ��̴��� ���� ����
};
//==========================================================
//todo : PxDeformableSuface �� ���� ����