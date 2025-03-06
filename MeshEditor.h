#pragma once
#include "Utility_Framework/Core.Minimal.h"
#include "RenderEngine/SceneRenderer.h"
#include "RenderEngine/Material.h"

class MeshEditor : public Singleton<MeshEditor>
{
public:
	bool show = false;
	std::string selectedModel{};
	SceneRenderer* sceneRenderer;

	void Initialize();
	void ReadyToEdit();
	void CheckChangeMesh();
	void ShowMainUI();
	void HandleConnection();

private:
	int _prevMeshId = -1;
	int _currMeshId = 0;
	std::shared_ptr<Material> dragDropMaterial;
};

static inline auto& MeshEditorSystem = MeshEditor::GetInstance();