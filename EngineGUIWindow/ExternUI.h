#pragma once
#ifndef YAML_CPP_API
#define YAML_CPP_API __declspec(dllimport)
#endif /* YAML_CPP_STATIC_DEFINE */
#include <string>
#include "yaml-cpp/yaml.h"

extern void DrawYamlNodeEditor(YAML::Node& node, const std::string& label = "");
extern void ImGuiDrawHelperMeshRenderer(class MeshRenderer* meshRenderer);
extern void ImGuiDrawHelperModuleBehavior(class ModuleBehavior* moduleBehavior);
extern void ImGuiDrawHelperAnimator(class Animator* animator);
extern void ImGuiDrawHelperPlayerInput(class PlayerInputComponent* playerInput);
extern void ImGuiDrawHelperRectTransformComponent(class RectTransformComponent* rectTransform);
extern void ImGuiDrawHelperTerrainComponent(class TerrainComponent* terrain);

extern void TextureDropTarget(class Material* mat);