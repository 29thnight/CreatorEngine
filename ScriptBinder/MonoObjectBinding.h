#pragma once
#include <mono/metadata/image.h>
#include <mono/metadata/object.h>

class GameObject;
class Component;

MonoObject* CreateManagedGameObject(GameObject* go);
MonoObject* CreateManagedComponent(Component* comp);

void RegisterObjectBindings(MonoImage* image);
void RegisterGameObjectBindings(MonoImage* image);
void RegisterComponentBindings(MonoImage* image);
