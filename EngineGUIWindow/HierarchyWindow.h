#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "ImGuiRegister.h"
#include "ImGui.h"

class SceneRenderer;
class GameObject;
class HierarchyWindow
{
public:
	HierarchyWindow(SceneRenderer* ptr);
	~HierarchyWindow() = default;

        void DrawSceneObject(const std::shared_ptr<GameObject>& obj);
        bool PassFilterRecursive(const std::shared_ptr<GameObject>& obj);

        SceneRenderer* m_sceneRenderer{ nullptr };
        ImGuiTextFilter m_searchFilter{};
};
#endif // !DYNAMICCPP_EXPORTS
