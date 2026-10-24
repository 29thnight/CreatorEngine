#include "HeadlessScene.h"
#include "../PhysicsBodyComponent.h"
#include "../ScriptComponent.h"

namespace Runtime::Headless
{
    void Session::TransferPersistentEntity(std::string_view name)
    {
        CheckOwner();
        if (!started_ || stopped_ || transferred_) throw std::logic_error("Invalid transfer boundary");
        Entity* entity = Entity::Find(name);
        if (!entity) throw std::runtime_error("Persistent entity was not found");
        auto* body = entity->GetComponent<PhysicsBodyComponent>();
        auto* script = entity->GetComponent<ScriptComponent>();
        if (!body || !script) throw std::runtime_error("Persistent test requires body and script");
        const auto before = body->ReadState();
        if (!before) throw std::runtime_error("Persistent body could not be read");
        const auto bodyHandle = body->RuntimeHandle();
        const auto handle = ScriptObjectRegistry::Get().Register(entity);
        const auto oldEntityHandle = scene_->HandleOf(entity->m_index);
        auto oldSceneRoot = scene_->root_from_this();
        const auto oldEpoch = manager_.SimulationCommands().Status().epoch;
        Object::SetDontDestroyOnLoad(entity);
        scene_ = manager_.CreateScene("AfterTransfer");
        if (!scene_) throw std::runtime_error("Persistent destination scene was not created");
        const auto after = body->ReadState();
        const auto samePose = after && std::abs(before->transform.position.x - after->transform.position.x) < 1e-5f
            && std::abs(before->linear_velocity.x - after->linear_velocity.x) < 1e-5f;
        if (ScriptObjectRegistry::Get().Resolve(handle) != entity || entity->GetScene() != scene_
            || entity->GetComponent<ScriptComponent>() != script || entity->GetComponent<PhysicsBodyComponent>() != body
            || oldSceneRoot->Resolve(oldEntityHandle) || !oldSceneRoot->IsManagedRetiringOrRetired()
            || bodyHandle.scene == body->RuntimeHandle().scene || !samePose
            || oldEpoch == manager_.SimulationCommands().Status().epoch)
            throw std::runtime_error("DDOL identity, state, stale handle, or epoch invariant failed");
        oldSceneRoot.reset();
        manager_.CollectManagedAtFrameBoundary();
        transferred_ = true;
    }
}
