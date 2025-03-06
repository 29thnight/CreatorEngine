#include "TestInteractableComponent.h"
#include "Physics/Common.h"
#include "RigidbodyComponent.h"
#include "Object.h"

bool TestInteractableComponent::IsInteractable()
{
    return false;
}

void TestInteractableComponent::Interact()
{
}

void TestInteractableComponent::Initialize()
{
}

void TestInteractableComponent::FixedUpdate(float fixedTick)
{
}

void TestInteractableComponent::Update(float tick)
{
}

void TestInteractableComponent::LateUpdate(float tick)
{
}

void TestInteractableComponent::EditorContext()
{
}

void TestInteractableComponent::OnTriggerEnter(ICollider* other)
{
    std::cout << "InteractEnter" << std::endl;
}

void TestInteractableComponent::OnTriggerStay(ICollider* other)
{
}

void TestInteractableComponent::OnTriggerExit(ICollider* other)
{
    std::cout << "InteractExit" << std::endl;
}

void TestInteractableComponent::OnCollisionEnter(ICollider* other)
{
}

void TestInteractableComponent::OnCollisionStay(ICollider* other)
{
}

void TestInteractableComponent::OnCollisionExit(ICollider* other)
{
}

void TestInteractableComponent::BurnOut()
{
}

void TestInteractableComponent::Interact(Object* obj)
{
    auto pos = obj->GetLocalPosition();
    pos.y += 0.1f;

    _owner->SetPosition(pos);

    auto rb = _owner->GetComponent<RigidbodyComponent>();
    auto mat = _owner->GetWorldMatrix();
    Vector3 posm(mat.Translation());
    Quaternion rot = Quaternion::CreateFromRotationMatrix(mat);
    rb->rbActor->setGlobalPose(PxTransform(PxVec3(posm.x, posm.y, posm.z), PxQuat(rot.x, rot.y, rot.z, rot.w)));

    srand(time(0));

    float x = (rand() / RAND_MAX) * 2.f - 1.f;
    float z = (rand() / RAND_MAX) * 2.f - 1.f;
    float vel[3] = { x * 4.f, 4.f, z * 4.f };

    rb->AddForce(vel, ForceMode::IMPULSE);

}
