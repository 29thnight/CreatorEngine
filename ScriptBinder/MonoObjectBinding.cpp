#include "MonoObjectBinding.h"
#include <mono/metadata/metadata.h>
#include <mono/jit/jit.h>
#include "MonoManager.h"
#include "SceneManager.h"
#include "Scene.h"
#include "GameObject.h"
#include "CSharpScriptComponent.h"
#include "DataSystem.h"

extern "C" intptr_t Internal_CreateGameObject()
{
    auto SharedGameObject = SceneManagers->GetActiveScene()->CreateGameObject("GameObject");
    return reinterpret_cast<intptr_t>(SharedGameObject.get());
}

extern "C" void Internal_DestroyGameObject(intptr_t ptr)
{
    auto pGameObject = reinterpret_cast<GameObject*>(ptr);
    pGameObject->Destroy();
}

extern "C" MonoObject* GameObject_GetComponentInternal(
    MonoObject* thisObj,
    MonoReflectionType* reflType)
{
    // Retrieve native GameObject*
    MonoClass* cls = mono_object_get_class(thisObj);
    MonoClassField* field = mono_class_get_field_from_name(cls, "nativeHandle");
    intptr_t goPtr; mono_field_get_value(thisObj, field, &goPtr);
    GameObject* go = reinterpret_cast<GameObject*>(goPtr);

    // Convert Type to Meta::Type
    MonoType* mtype = mono_reflection_type_get_type(reflType);
    MonoClass* typeCls = mono_type_get_class(mtype);
    const char* ns = mono_class_get_namespace(typeCls);
    const char* name = mono_class_get_name(typeCls);
    std::string fullname = std::string(ns) + "." + name;
    const Meta::Type* meta = Meta::MetaDataRegistry->Find(fullname);

    Component* comp = go->GetComponent(*meta).get();
    // Create and return managed wrapper
    return CreateManagedComponent(comp);
}

extern "C" MonoObject* GameObject_AddComponentInternal(
    MonoObject* thisObj,
    MonoReflectionType* reflType)
{
    MonoClassField* field = mono_class_get_field_from_name(
        mono_object_get_class(thisObj), "nativeHandle");
    intptr_t goPtr; mono_field_get_value(thisObj, field, &goPtr);
    GameObject* go = reinterpret_cast<GameObject*>(goPtr);

    MonoType* mtype = mono_reflection_type_get_type(reflType);
    MonoClass* typeCls = mono_type_get_class(mtype);
    const char* name = mono_class_get_name(typeCls);

    MonoObject* managedObj = nullptr;
    Component* comp = nullptr;

    if (auto meta = Meta::MetaDataRegistry->Find(name))
    {
        comp = go->AddComponent(*meta).get();
        managedObj = CreateManagedComponent(comp);
    }
    else
    {
        MonoClass* mbCls = mono_class_from_name(MonoManagers->GetImage(), "Engine", name);
        managedObj = mono_object_new(MonoManagers->GetDomain(), mbCls);
        mono_runtime_object_init(managedObj);

        std::string csFile = name;
        csFile += ".cs";

        auto guid = DataSystems->GetFilenameToGuid(csFile);
        go->AddComponent<CSharpScriptComponent>()->ManagedDataInitialize(managedObj, name, guid);
    }

    return managedObj;
}

extern "C" void Destroy_Injected(intptr_t gcHandlePtr, float t = 0)
{
    auto pGameObject = reinterpret_cast<GameObject*>(gcHandlePtr);
    pGameObject->Destroy();
}

extern "C" MonoObject* Component_GetGameObject_Injected(MonoObject* thisObj)
{
    MonoClass* cls = mono_object_get_class(thisObj);
    MonoClassField* field = mono_class_get_field_from_name(cls, "nativeHandle");
    intptr_t compPtr;
    mono_field_get_value(thisObj, field, &compPtr);
    Component* comp = reinterpret_cast<Component*>(compPtr);

    GameObject* owner = comp->GetOwner();
    return CreateManagedGameObject(owner);
}

MonoObject* CreateManagedGameObject(GameObject* go)
{
    if (!go) return nullptr;
    MonoClass* cls = mono_class_from_name(MonoManagers->GetImage(), "Engine", "GameObject");
    MonoObject* obj = mono_object_new(MonoManagers->GetDomain(), cls);
    mono_runtime_object_init(obj);
    MonoClassField* fh = mono_class_get_field_from_name(cls, "nativeHandle");
    intptr_t ptrVal = reinterpret_cast<intptr_t>(go);
    mono_field_set_value(obj, fh, &ptrVal);
    return obj;
}

MonoObject* CreateManagedComponent(Component* comp)
{
    if (!comp) return nullptr;
    MonoClass* cls = mono_class_from_name(MonoManagers->GetImage(), "Engine", "Component");
    MonoObject* obj = mono_object_new(MonoManagers->GetDomain(), cls);
    mono_runtime_object_init(obj);
    MonoClassField* fh = mono_class_get_field_from_name(cls, "nativeHandle");
    intptr_t ptrVal = reinterpret_cast<intptr_t>(comp);
    mono_field_set_value(obj, fh, &ptrVal);
    return obj;
}

void RegisterObjectBindings(MonoImage* image)
{
    mono_add_internal_call("Engine.Object::Destroy_Injected",
                            reinterpret_cast<void*>(Destroy_Injected));
}

void RegisterGameObjectBindings(MonoImage* image)
{
    mono_add_internal_call("Engine.GameObject::Internal_CreateGameObject",
                        reinterpret_cast<void*>(Internal_CreateGameObject));
    mono_add_internal_call("Engine.GameObject::Internal_DestroyGameObject",
                            reinterpret_cast<void*>(Internal_DestroyGameObject));
    mono_add_internal_call("Engine.GameObject::GetComponentInternal",
                            reinterpret_cast<void*>(GameObject_GetComponentInternal));
    mono_add_internal_call("Engine.GameObject::AddComponentInternal",
                            reinterpret_cast<void*>(GameObject_AddComponentInternal));
}

void RegisterComponentBindings(MonoImage* image)
{
    mono_add_internal_call("Engine.Component::get_gameObject_Injected",
                            reinterpret_cast<void*>(Component_GetGameObject_Injected));
}
