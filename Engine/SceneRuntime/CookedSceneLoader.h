#pragma once
#include "../Utility_Framework/Foundation/StandardLibrary.h"
class Scene;
class Entity;
class Component;
namespace Cedo { struct Node; }

// CEDO admission into the ordinary Scene graph. Owns no runtime entities or services.
class CookedSceneLoader final
{
public:
    // CPU packages can install additional native component readers without
    // changing Scene, Entity, or this loader. Readers use the existing typed
    // component factory/lifecycle registration and must return an attached owner.
    using ComponentReader = Component* (*)(Entity&, const Cedo::Node&);
    static void RegisterComponentReader(std::string typeName, ComponentReader reader);

    static void Load(Scene& scene, const std::filesystem::path& path);
};
