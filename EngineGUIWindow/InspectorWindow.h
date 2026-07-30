#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "ImGuiRegister.h"
#include <imgui.h>
#include <imgui_internal.h>
#include "CurvePoint.h"

class SceneRenderer;
class GameObject;
class InspectorWindow
{
public:
	InspectorWindow(SceneRenderer* ptr);
	~InspectorWindow() = default;

private:
	SceneRenderer* m_sceneRenderer{ nullptr };
	bool m_openScriptPopup{ false };
	bool m_openNewScriptPopup{ false };
	bool m_openNewTagPopup{ false };
	bool m_openNewLayerPopup{ false };
	bool m_openFSMPopup{ false };
	bool m_openBTPopup{ false };

	// C# 스크립트의 노출 필드를 그린다.
	// 값 접근은 소스 제너레이터가 만든 인덱스 기반 접근자를 통한다(ClrHost 경유).
	void DrawManagedScripts(class ScriptComponent* script);

	void ImGuiDrawHelperGameObjectBaseInfo(GameObject* gameObject);
	void ImGuiDrawHelperTransformComponent(GameObject* gameObject);
	void ImGuiDrawHelperFSM(class StateMachineComponent* FSMComponent);
	void ImGuiDrawHelperBT(class BehaviorTreeComponent* BTComponent);
	void ImGuiDrawHelperVolume(class VolumeComponent* volumeComponent);
	void ImGuiDrawHelperDecal(class DecalComponent* decalComponent);
	void ImGuiDrawHelperImageComponent(class ImageComponent* imageComponent);
	void ImGuiDrawHelperSpriteRenderer(class SpriteRenderer* spriteRenderer);
	void ImGuiDrawHelperCanvas(class Canvas* canvas);
	void ImGuiDrawHelperSoundComponent(class SoundComponent* sc);

	bool DrawRolloffCurveEditor(std::vector<CurvePoint>& curve,
		float maxDist,
		ImVec2 size = ImVec2(0, 180),
		int* outSelected = nullptr,
		bool readOnly = false);
	void DrawSoundClipPicker();

private:
	bool            m_openClipPicker{ false };
	SoundComponent* m_clipPickerTarget{ nullptr };
	std::string     m_clipSearch;                 // �˻���
	std::vector<std::string> m_clipKeyCache;      // ĳ��
};
#endif // !DYNAMICCPP_EXPORTS