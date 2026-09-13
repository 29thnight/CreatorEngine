#include "EditorComponentCatalog.h"
#include <iostream>
#include <stdexcept>

int main()
{
    using namespace editor::components;
    int checks = 0;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
        ++checks;
    };
    check(Native("CameraComponent").label == "Camera", "Readable native name");
    check(Native("MeshRenderer").label == "Mesh Renderer", "Word boundary");
    check(Native("UIButton").label == "UI Button", "Acronym boundary");
    check(Script("Game.AI.PlayerController").label == "Player Controller", "Managed display name");
    check(Script("Component").label == "Component", "Script names retain Component suffix");
    for (const auto* type : {"BoxColliderComponent", "SphereColliderComponent", "CapsuleColliderComponent", "RigidBodyComponent", "CharacterControllerComponent", "RagdollComponent"})
        check(Native(type).category == "Physics", "Physics category");
    for (const auto* type : {"CameraComponent", "LightComponent", "MeshRenderer", "SpriteRenderer", "DecalComponent", "VolumeComponent", "TerrainComponent", "FoliageComponent"})
        check(Native(type).category == "Rendering", "Rendering category");
    for (const auto* type : {"Canvas", "ImageComponent", "TextComponent", "UIButton"})
        check(Native(type).category == "UI", "UI category");
    check(Native("NewEngineFeature").category == "Other", "Uncategorized types remain discoverable");
    check(Script("Bobber").category == "Scripts", "Script category");
    check(Matches(Native("MeshRenderer"), "mEsH reN"), "Case-insensitive multiword search");
    check(Matches(Native("BoxColliderComponent"), "PHYSICS box"), "Category search");
    check(Matches(Native("BoxColliderComponent"), "BoxCollider"), "Raw class search");
    check(Matches(Script("Game.PlayerController"), "game.player scripts"), "Script namespace search");
    check(!Matches(Native("BoxColliderComponent"), "box audio"), "All search terms required");
    check(Matches(Native("LightComponent"), " \t "), "Whitespace search");
    check(!Matches(Native("LightComponent"), "no_such_component"), "Empty result");
    std::vector<Entry> entries{Script("B.Player"), Native("CameraComponent"), Script("A.Player")};
    Sort(entries);
    check(entries[0].type == "CameraComponent" && entries[1].type == "A.Player" && entries[2].type == "B.Player", "Stable disambiguation order");
    std::cout << "COMPONENT_CATALOG_OK checks=" << checks << '\n';
}
