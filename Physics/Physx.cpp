#include "pch.h"
#include "Physx.h"
#include "IRigidbody.h"
#include "PhysicsInfo.h"
#include "PhysicsEventCallback.h"
#include <vector>
#include <tuple>
#include <mutex>
#include <iostream>

#include "ICollider.h"

static PhysicsEventCallback eventCallback;

// block(default), player, enemy, interact
bool collisionMatrix[4][4] = {
    {true, true, true, true},
    {true, true, true, true},
    {true, true, false, false},
    {true, true, false, false}
};

bool intersectionMatrix[3][3] = {
	{1, 1, 1},
	{1, 1, 1},
    {1, 1, 0},
};

PxFilterFlags CustomFilterShader(
    PxFilterObjectAttributes at0,
    PxFilterData fd0,
    PxFilterObjectAttributes at1,
    PxFilterData fd1,
    PxPairFlags& pairFlags, const void* constantBlock, PxU32 constantBlockSize)
{
    int group0 = fd0.word0;
    int group1 = fd1.word0;

    if (!collisionMatrix[group0][group1]) {
        return PxFilterFlag::eSUPPRESS; // �浹 ����
    }


    pairFlags = PxPairFlag::eCONTACT_DEFAULT;
    pairFlags |= PxPairFlag::eTRIGGER_DEFAULT;

    pairFlags |= PxPairFlag::eNOTIFY_TOUCH_FOUND;
    pairFlags |= PxPairFlag::eNOTIFY_TOUCH_PERSISTS;
    pairFlags |= PxPairFlag::eNOTIFY_TOUCH_LOST;
    pairFlags |= PxPairFlag::eNOTIFY_CONTACT_POINTS;

    return PxFilterFlag::eDEFAULT;
}

void PhysicX::Initialize()
{
    m_foundation = PxCreateFoundation(PX_PHYSICS_VERSION, m_allocator, m_errorCallback);     // ���� ���

#if _DEBUG
    // PVD ����
    pvd = PxCreatePvd(*m_foundation);
    PxPvdTransport* transport = PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
    auto isconnected = pvd->connect(*transport, PxPvdInstrumentationFlag::eALL);
#endif

    m_physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_foundation, physx::PxTolerancesScale(1.f, 40.f), recordMemoryAllocations, pvd);  // ���� ����� ������� ������ ����.

    m_defaultMaterial = m_physics->createMaterial(1.f, 1.f, 0.f);

    gDispatcher = PxDefaultCpuDispatcherCreate(2);  // �޸�Ǯ ��.

    // Scene ����
    physx::PxSceneDesc sceneDesc(m_physics->getTolerancesScale());
    sceneDesc.gravity = physx::PxVec3(0.0f, -9.81f, 0.0f);

    // �� ���� �� �� 2������ �־�� �ϴµ�. ������ Scene�� null��.
    sceneDesc.cpuDispatcher = gDispatcher;
    //sceneDesc.filterShader = PxDefaultSimulationFilterShader;
    sceneDesc.filterShader = CustomFilterShader;

    sceneDesc.simulationEventCallback = &eventCallback;

    //sceneDesc.kineKineFilteringMode = PxPairFilteringMode::eKEEP;

    m_scene = m_physics->createScene(sceneDesc);

    m_controllerManager = PxCreateControllerManager(*m_scene);

    // ����� �ð�ȭ �÷��� ����
    m_scene->setVisualizationParameter(PxVisualizationParameter::eSCALE, 1.0f); // ��ü ������
    m_scene->setVisualizationParameter(PxVisualizationParameter::eCOLLISION_SHAPES, 1.0f); // �浹 ���� �ð�ȭ
    m_scene->setVisualizationParameter(PxVisualizationParameter::eACTOR_AXES, 1.0f);       // ���� �� �ð�ȭ
    m_scene->setVisualizationParameter(PxVisualizationParameter::eCONTACT_POINT, 1.0f);   // ������ �ð�ȭ
    m_scene->setVisualizationParameter(PxVisualizationParameter::eJOINT_LOCAL_FRAMES, 1.0f); // ����Ʈ ������ �ð�ȭ
}

void PhysicX::UnInitialize()
{
    Physics->ShowNotRelease();

    if (gDispatcher) gDispatcher->release();
    

    if (m_controllerManager) {
        PxU32 controllerCount = m_controllerManager->getNbControllers();

        for (PxI32 i = static_cast<PxI32>(controllerCount) - 1; i >= 0; --i) {
            PxController* controller = m_controllerManager->getController(i);
            if (controller) {
                controller->release();  // ��Ʈ�ѷ� ����
            }
        }
        m_controllerManager->release();
    }
    if (m_scene) {
        PxU32 actorCount = m_scene->getNbActors(PxActorTypeFlag::eRIGID_STATIC | PxActorTypeFlag::eRIGID_DYNAMIC);
        std::vector<PxActor*> actors(actorCount);
        m_scene->getActors(PxActorTypeFlag::eRIGID_STATIC | PxActorTypeFlag::eRIGID_DYNAMIC, actors.data(), actorCount);

        for (PxActor* actor : actors) {
            m_scene->removeActor(*actor);
            actor->release();
        }

        m_scene->release();
    }

    if (pvd) {
        if (pvd->isConnected()) {
            pvd->disconnect();
        }
        PxPvdTransport* transport = pvd->getTransport();
        if (transport) {
            transport->release();
        }
        pvd->release();
    }

    if (m_physics) m_physics->release();
    if (m_foundation) m_foundation->release();
}

void PhysicX::PreUpdate()
{

    // ��ü���� ���� actor�� �Է��ϴ� �ܰ�.
    PxU32 curActorCount = m_scene->getNbActors(PxActorTypeFlag::eRIGID_DYNAMIC);
    std::vector<PxActor*> actors(curActorCount);
    m_scene->getActors(PxActorTypeFlag::eRIGID_DYNAMIC, actors.data(), curActorCount);

    for (int i = 0; i < curActorCount; i++) {
        auto dynamicActor = static_cast<PxRigidDynamic*>(actors[i]);
        IRigidbody* rb = static_cast<IRigidbody*>(actors[i]->userData);

        RigidbodyInfo* info = rb->GetInfo();

        if (info->isCharacterController) continue;

        //if (info->changeflag == false) continue;  // ��������� ���� ���� �����ϵ��� �ϰ������
        //info->changeflag = false;                 // const���ٸ��� �� �� ����� �о �ϰ�ʹ�.

        // �ùķ��̼��� actor ������Ʈ
        dynamicActor->setMass(info->mass);
        dynamicActor->setLinearDamping(info->drag);
        dynamicActor->setAngularDamping(info->angularDrag);
        dynamicActor->setCMassLocalPose(PxTransform(info->centerOfMass[0], info->centerOfMass[1], info->centerOfMass[2]));
        dynamicActor->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !info->useGravity);
        dynamicActor->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, info->isKinematic);

        // ��ġ, ȸ�� ������Ʈ
        auto [posX, posY, posZ] = rb->GetWorldPosition();
        auto [rotX, rotY, rotZ, rotW] = rb->GetWorldRotation();

        // ���� ��ġ, ȸ�� ����.
        info->prePosition[0] = posX;
        info->prePosition[1] = posY;
        info->prePosition[2] = posZ;

        info->preRotation[0] = rotX;
        info->preRotation[1] = rotY;
        info->preRotation[2] = rotZ;
        info->preRotation[3] = rotW;

        PxTransform p({ posX, posY, posZ }, { rotX, rotY, rotZ, rotW });
        //if (info->isKinematic)
        //    dynamicActor->setKinematicTarget(p); // Ű�׸�ƽ�� �� �̵�ó��
        //else
        if(!info->isCharacterController)
            dynamicActor->setGlobalPose(p);     // �⺻ �̵��� ���� ������Ʈ.
    }
}

void PhysicX::Update(float fixedDeltaTime)
{
    if (fixedDeltaTime > 0.f) {
        m_scene->simulate(fixedDeltaTime);
        /////-----------------------��-------------------------
        m_scene->fetchResults(true);    //true�� block��. ������ ����ҰŸ� false���.
    }
}

void PhysicX::PostUpdate()
{
    // ��ü���� ���� actor���� �޾ƿ��� �ܰ�.
    PxU32 curActorCount = m_scene->getNbActors(PxActorTypeFlag::eRIGID_DYNAMIC);
    std::vector<PxActor*> actors(curActorCount);
    m_scene->getActors(PxActorTypeFlag::eRIGID_DYNAMIC, actors.data(), curActorCount);

    for (int i = 0; i < curActorCount; i++) {
        auto dynamicActor = static_cast<PxRigidDynamic*>(actors[i]);
        IRigidbody* rb = static_cast<IRigidbody*>(actors[i]->userData);

        //if (rb->GetInfo()->isKinematic == true)
        //    continue; // Kinematic�� ���� ����� �ݿ����� ����. (trigger�� �ϴϱ� ���� �ۼ�)

        auto pose = dynamicActor->getGlobalPose();
        auto pos = pose.p;
        auto rot = pose.q;

        rb->SetGlobalPosAndRot(std::make_tuple(pos.x, pos.y, pos.z), 
            std::make_tuple(rot.x, rot.y, rot.z, rot.w));
    }
}

void PhysicX::PostUpdate(float interpolated)
{
    // ��ü���� ���� actor���� �޾ƿ��� �ܰ�.
    PxU32 curActorCount = m_scene->getNbActors(PxActorTypeFlag::eRIGID_DYNAMIC);
    std::vector<PxActor*> actors(curActorCount);
    m_scene->getActors(PxActorTypeFlag::eRIGID_DYNAMIC, actors.data(), curActorCount);

    for (int i = 0; i < curActorCount; i++) {
        auto dynamicActor = static_cast<PxRigidDynamic*>(actors[i]);
        IRigidbody* rb = static_cast<IRigidbody*>(actors[i]->userData);
        auto info = rb->GetInfo();


       // if (info->isKinematic == true)
        //    continue; // Kinematic�� ���� ����� �ݿ����� ����. (trigger�� �ϴϱ� ���� �ۼ�)

        auto pose = dynamicActor->getGlobalPose();
        auto pos = pose.p;
        auto rot = pose.q;

        auto interpolatedPos = Lerp(
            PxVec3(info->prePosition[0], info->prePosition[1], info->prePosition[2]),
            pos, interpolated);
        auto interpolatedRot = Slerp(
            PxQuat(info->preRotation[0], info->preRotation[1], info->preRotation[2], info->preRotation[3]),
            rot, interpolated);

        rb->SetGlobalPosAndRot(std::make_tuple(interpolatedPos.x, interpolatedPos.y, interpolatedPos.z),
            std::make_tuple(interpolatedRot.x, interpolatedRot.y, interpolatedRot.z, interpolatedRot.w));
    }
}

void PhysicX::AddRigidBody()
{
}

void PhysicX::ClearActors()
{
    PxU32 numMeshes = m_physics->getNbConvexMeshes();
    std::vector<PxConvexMesh*> meshes(numMeshes);
    m_physics->getConvexMeshes(meshes.data(), numMeshes);
    for (PxConvexMesh* mesh : meshes) {
        mesh->release();
    }

    //Physics->ShowNotRelease();

    //m_scene->flushSimulation(true);

    //// controller ����
    //PxU32 numControllers = m_controllerManager->getNbControllers();
    //for (PxU32 i = 0; i < numControllers; ++i) {
    //    PxController* controller = m_controllerManager->getController(i);
    //    controller->release();
    //}
    //m_controllerManager->purgeControllers();

    //// actor ����
    //PxU32 actorCount = m_scene->getNbActors(PxActorTypeFlag::eRIGID_DYNAMIC | PxActorTypeFlag::eRIGID_STATIC);
    //std::vector<PxActor*> actors(actorCount);
    //m_scene->getActors(PxActorTypeFlag::eRIGID_DYNAMIC | PxActorTypeFlag::eRIGID_STATIC, actors.data(), actorCount);
    //for (PxActor* actor : actors) {
    //    m_scene->removeActor(*actor);
    //    actor->release();
    //}

    // ���Ƿ� �Ҹ� �����Ͽ� Ŭ���� �ϴ� ��������� ������ �ִٸ� ���濹��.
    //UnInitialize();
    //if (m_scene) {
    //    m_scene->fetchResults(true);  // �ùķ��̼� ���� ���
    //}

    //for (PxU32 i = 0; i < m_scene->getNbActors(PxActorTypeFlag::eRIGID_DYNAMIC | PxActorTypeFlag::eRIGID_STATIC); i++) {
    //    PxActor* actor;
    //    m_scene->getActors(PxActorTypeFlag::eRIGID_DYNAMIC | PxActorTypeFlag::eRIGID_STATIC, &actor, 1, i);
    //    m_scene->removeActor(*actor);
    //    actor->release(); // ������ ������
    //}

    //// 2? ��� ĳ���� ��Ʈ�ѷ� ����
    //if (m_controllerManager) {
    //    const PxU32 controllerCount = m_controllerManager->getNbControllers();
    //    for (PxI32 i = static_cast<PxI32>(controllerCount) - 1; i >= 0; --i) {
    //        PxController* controller = m_controllerManager->getController(i);
    //        if (controller) {
    //            controller->release();
    //            controller = nullptr;  // ���� ���� ����
    //        }
    //    }
    //    m_controllerManager->release();
    //    m_controllerManager = nullptr;
    //}

    //// 3? ����ó ����
    //if (gDispatcher) {
    //    gDispatcher->release();
    //    gDispatcher = nullptr;
    //}

    //// 4? ���� ����(Material) ����
    //if (m_defaultMaterial) {
    //    m_defaultMaterial->release();
    //    m_defaultMaterial = nullptr;
    //}

    //// 5? Scene ����
    //if (m_scene) {
    //    m_scene->release();
    //    m_scene = nullptr;
    //}

    //// 6? Physics ����
    //if (m_physics) {
    //    m_physics->release();
    //    m_physics = nullptr;
    //}

    //// 7? PVD ���� (���� ���� �� ����)
    //if (pvd) {
    //    if (pvd->isConnected()) {
    //        pvd->disconnect();  // PVD ���� ����
    //    }
    //    pvd->release();
    //    pvd = nullptr;
    //}

    //// 8? Foundation ���� (���� �������� ����)
    //if (m_foundation) {
    //    m_foundation->release();
    //    m_foundation = nullptr;
    //}

    //Initialize();
}

void PhysicX::ConnectPVD()
{
    if (pvd != nullptr) {
        if (pvd->isConnected())
            pvd->disconnect();
    }
    pvd = PxCreatePvd(*m_foundation);
    PxPvdTransport* transport = PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
    auto isconnected = pvd->connect(*transport, PxPvdInstrumentationFlag::eALL);
    std::cout << "pvd connected : " << isconnected << std::endl;
}

void PhysicX::ShowNotRelease()
{
    std::cout << "physx count " << std::endl;
   std::cout << m_physics->getNbBVHs()<< std::endl;
   std::cout << m_physics->getNbBVHs()<< std::endl;
   std::cout << m_physics->getNbConvexMeshes()<< std::endl;
   std::cout << m_physics->getNbDeformableSurfaceMaterials()<< std::endl;
   std::cout << m_physics->getNbDeformableVolumeMaterials()<< std::endl;
   std::cout << m_physics->getNbFEMSoftBodyMaterials()<< std::endl;
   std::cout << m_physics->getNbHeightFields()<< std::endl;
   std::cout << m_physics->getNbMaterials()<< std::endl; // 1
   std::cout << m_physics->getNbPBDMaterials()<< std::endl;
   std::cout << m_physics->getNbScenes()<< std::endl;  // 1
   std::cout << m_physics->getNbShapes()<< std::endl;  // 3
   std::cout << m_physics->getNbTetrahedronMeshes()<< std::endl;
   std::cout << m_physics->getNbTriangleMeshes() << std::endl;
}

void PhysicX::GetShapes(std::vector<BoxShape>& out)
{
    PxU32 curActorCount = m_scene->getNbActors(PxActorTypeFlag::eRIGID_DYNAMIC);
    std::vector<PxActor*> actors(curActorCount);
    m_scene->getActors(PxActorTypeFlag::eRIGID_DYNAMIC, actors.data(), curActorCount);

    for (PxU32 i = 0; i < curActorCount; i++)
    {
        PxRigidActor* actor = static_cast<PxRigidActor*>(actors[i]);
        PxTransform globalPose = actor->getGlobalPose();

        PxU32 shapeCount = actor->getNbShapes();
        std::vector<PxShape*> shapes(shapeCount);
        actor->getShapes(shapes.data(), shapeCount);

        for (PxU32 j = 0; j < shapeCount; j++)
        {
            PxShape* shape = shapes[j];
            PxTransform localPose = shape->getLocalPose();
            PxTransform worldColliderPose = globalPose * localPose;

            PxBoxGeometry box;
            if (shape->getGeometry().getType() == PxGeometryType::eBOX)
            {
                PxGeometryHolder holder(shape->getGeometry());
                
                auto extent = holder.box().halfExtents;
                BoxShape boxshape;
                boxshape.worldPosition[0] = worldColliderPose.p.x / 10.f;
                boxshape.worldPosition[1] = worldColliderPose.p.y / 10.f;
                boxshape.worldPosition[2] = worldColliderPose.p.z / 10.f;
                boxshape.worldRotation[0] = worldColliderPose.q.x;
                boxshape.worldRotation[1] = worldColliderPose.q.y;
                boxshape.worldRotation[2] = worldColliderPose.q.z;
                boxshape.worldRotation[3] = worldColliderPose.q.w;
                boxshape.halfSize[0] = extent.x / 10.f;
                boxshape.halfSize[1] = extent.y / 10.f;
                boxshape.halfSize[2] = extent.z / 10.f;

                out.push_back(boxshape);
            }
        }
    }
}
