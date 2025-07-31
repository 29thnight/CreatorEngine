#pragma once
#ifndef YAML_CPP_API
#define YAML_CPP_API __declspec(dllimport)
#endif /* YAML_CPP_STATIC_DEFINE */
#include <string>
#include "yaml-cpp/yaml.h"

class MeshRenderer;
class ModuleBehavior;
class Animator;
class PlayerInputComponent;
extern void DrawYamlNodeEditor(YAML::Node& node, const std::string& label = "");
extern void ImGuiDrawHelperMeshRenderer(MeshRenderer* meshRenderer);
extern void ImGuiDrawHelperModuleBehavior(ModuleBehavior* moduleBehavior);
extern void ImGuiDrawHelperAnimator(Animator* animator);
extern void ImGuiDrawHelperPlayerInput(PlayerInputComponent* playerInput);