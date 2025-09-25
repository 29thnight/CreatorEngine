#pragma once
#include "Core.Minimal.h"
#include "Scene.h"

/**
 * @brief Base interface for event-receiving components, compatible with reflection.
 *
 * This class defines the event hooks and a pure virtual function for registration.
 * Components should not inherit from this directly. Instead, they should inherit
 * from the CRTP helper class: RegistableEvent<T>.
 */
class IRegistableEvent
{
public:
    virtual ~IRegistableEvent();

    // --- Lifecycle Event Hooks ---
    // These are meant to be overridden by the final component class.
    virtual void Awake() {}
    virtual void Start() {}
    virtual void Update(float deltaTime) {}
    virtual void LateUpdate(float deltaTime) {}
    virtual void FixedUpdate(float timeStep) {}
    virtual void OnEnable() {}
    virtual void OnDisable() {}
    virtual void OnDestroy() {}

    /**
     * @brief Pure virtual function to trigger the event registration process.
     *
     * This function is called via a pointer to this base interface, allowing
     * the registration to be initiated without knowing the derived type at compile time.
     * The actual implementation is in the CRTP helper class.
     * @param scene The scene to register events with.
     */
    virtual void RegisterOverriddenEvents(Scene* scene) = 0;

protected:
    friend class SceneObject;

    /**
     * @brief Unregisters the component from all events it was subscribed to.
     */
    void UnregisterEvents();

    Scene* subscribedScene{};
    Core::DelegateHandle m_awakeEventHandle{};
    Core::DelegateHandle m_startEventHandle{};
    Core::DelegateHandle m_updateEventHandle{};
    Core::DelegateHandle m_lateUpdateEventHandle{};
    Core::DelegateHandle m_fixedUpdateEventHandle{};
    Core::DelegateHandle m_onEnableEventHandle{};
    Core::DelegateHandle m_onDisableEventHandle{};
    Core::DelegateHandle m_onDestroyEventHandle{};
    bool isAwakeCalled{ false };
    bool isStartCalled{ false };
	bool isPrevEnabled{ true };
};


/**
 * @brief CRTP (Curiously Recurring Template Pattern) helper class.
 *
 * User components (e.g., MyMover) should inherit from this class like so:
 * class MyMover : public RegistableEvent<MyMover> { ... };
 *
 * This class implements the registration logic by inspecting the derived type 'T'.
 * @tparam T The actual type of the component that inherits from this.
 */
template<typename T>
class RegistableEvent : public IRegistableEvent
{
public:
    // Override the pure virtual function from the base interface.
    // 'final' keyword prevents further overriding and can help compiler optimizations.
    void RegisterOverriddenEvents(Scene* scene) final override;
};


// --- Implementation Details (Must be in header) ---

template<typename T>
void RegistableEvent<T>::RegisterOverriddenEvents(Scene* scene)
{
    if (!scene) return;
    this->subscribedScene = scene;

    // Cast 'this' to the actual derived component type.
    T* derived_component = static_cast<T*>(this);
	bool& isAwakeCalled = this->isAwakeCalled;
	bool& isStartCalled = this->isStartCalled;

    // Use 'if constexpr' (C++17) to check for overridden methods at compile time.
    // This is the core of the optimization: we only generate registration code
    // and subscribe to events if the component has actually implemented the method.

    if constexpr (&T::Awake != &IRegistableEvent::Awake) {
        this->m_awakeEventHandle = scene->AwakeEvent.AddLambda([derived_component, &isAwakeCalled]()
        { 
            auto sceneObject = derived_component->GetOwner();
            if (!derived_component->IsEnabled() || sceneObject->IsDestroyMark())
            {
                return;
            }
            else if (!isAwakeCalled)
            {
                isAwakeCalled = true;
                derived_component->Awake();
            }
        });
    }
    if constexpr (&T::Start != &IRegistableEvent::Start) {
        this->m_startEventHandle = scene->StartEvent.AddLambda([derived_component, &isStartCalled]()
        { 
            auto sceneObject = derived_component->GetOwner();
            if (!derived_component->IsEnabled() || sceneObject->IsDestroyMark())
            {
                return;
            }
            else if (!isStartCalled)
            {
                derived_component->Start();
                isStartCalled = true;
            }
        });
    }
    if constexpr (&T::Update != &IRegistableEvent::Update) {
        this->m_updateEventHandle = scene->UpdateEvent.AddLambda([derived_component](float dt) 
        { 
            auto sceneObject = derived_component->GetOwner();
            if (!derived_component->IsEnabled() || sceneObject->IsDestroyMark())
            {
                return;
            }
            else
            {
                derived_component->Update(dt);
            }
        });
    }
    if constexpr (&T::LateUpdate != &IRegistableEvent::LateUpdate) {
        this->m_lateUpdateEventHandle = scene->LateUpdateEvent.AddLambda([derived_component](float dt)
        { 
            auto sceneObject = derived_component->GetOwner();
            if (!derived_component->IsEnabled() || sceneObject->IsDestroyMark())
            {
                return;
            }
            else
            {
                derived_component->LateUpdate(dt);
            }
        });
    }
    if constexpr (&T::FixedUpdate != &IRegistableEvent::FixedUpdate) {
        this->m_fixedUpdateEventHandle = scene->FixedUpdateEvent.AddLambda([derived_component](float ts) 
        { 
            auto sceneObject = derived_component->GetOwner();
            if (!derived_component->IsEnabled() || sceneObject->IsDestroyMark())
            {
                return;
            }
            else
            {
                derived_component->FixedUpdate(ts);
            }
        });
    }
    if constexpr (&T::OnEnable != &IRegistableEvent::OnEnable) {
        this->m_onEnableEventHandle = scene->OnEnableEvent.AddLambda([derived_component]() 
        { 
            auto sceneObject = derived_component->GetOwner();
            if (sceneObject)
            {
                bool isCurrEnabled = derived_component->IsEnabled();
                if (isCurrEnabled != isPrevEnabled && false == isPrevEnabled)
                {
                    derived_component->OnEnable();
                    isPrevEnabled = isCurrEnabled;
                }
            }
        });
    }
    if constexpr (&T::OnDisable != &IRegistableEvent::OnDisable) {
        this->m_onDisableEventHandle = scene->OnDisableEvent.AddLambda([derived_component]() 
        { 
            auto sceneObject = derived_component->GetOwner();
            if (sceneObject)
            {
                bool isCurrEnabled = derived_component->IsEnabled();
                if (isCurrEnabled != isPrevEnabled && true == isPrevEnabled)
                {
                    derived_component->OnDisable();
                    isPrevEnabled = isCurrEnabled;
                }
            }
        });
    }
    if constexpr (&T::OnDestroy != &IRegistableEvent::OnDestroy) {
        this->m_onDestroyEventHandle = scene->OnDestroyEvent.AddLambda([derived_component]() 
        { 
            auto sceneObject = derived_component->GetOwner();
            if (nullptr == sceneObject)
            {
                Debug->LogCritical("IOnDestroy::OnDestroy called on invalid component or scene object.");
                return;
            }

            if (sceneObject->IsDestroyMark() || derived_component->IsDestroyMark())
            {
                derived_component->OnDestroy();
            }
        });
    }
}

// Define the destructor and UnregisterEvents implementation.
// 'inline' is appropriate here as they are defined in the header.
inline IRegistableEvent::~IRegistableEvent()
{
    UnregisterEvents();
}

inline void IRegistableEvent::UnregisterEvents()
{
    if (!subscribedScene) return;

    // Assumes DelegateHandle has a way to check for validity (e.g., IsValid() or operator bool)
    if (m_awakeEventHandle.IsValid()) subscribedScene->AwakeEvent.Remove(m_awakeEventHandle);
    if (m_startEventHandle.IsValid()) subscribedScene->StartEvent.Remove(m_startEventHandle);
    if (m_updateEventHandle.IsValid()) subscribedScene->UpdateEvent.Remove(m_updateEventHandle);
    if (m_lateUpdateEventHandle.IsValid()) subscribedScene->LateUpdateEvent.Remove(m_lateUpdateEventHandle);
    if (m_fixedUpdateEventHandle.IsValid()) subscribedScene->FixedUpdateEvent.Remove(m_fixedUpdateEventHandle);
    if (m_onEnableEventHandle.IsValid()) subscribedScene->OnEnableEvent.Remove(m_onEnableEventHandle);
    if (m_onDisableEventHandle.IsValid()) subscribedScene->OnDisableEvent.Remove(m_onDisableEventHandle);
    if (m_onDestroyEventHandle.IsValid()) subscribedScene->OnDestroyEvent.Remove(m_onDestroyEventHandle);
}