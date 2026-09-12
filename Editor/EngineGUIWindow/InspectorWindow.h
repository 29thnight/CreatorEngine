#pragma once
#include "ImGui.h"
#include <imgui.h>
#include <imgui_internal.h>
#include "CurvePoint.h"
#include "EditorPropertyRow.h" // 배치 계약 (W2-I2)

class Entity;
class InspectorWindow
{
public:
	void Draw();
	~InspectorWindow() = default;

private:
	// 폭 전환의 직전 모드. 완충 폭과 편집 중 보류가 이것을 본다. 숨은 전역이
	// 아니라 창이 들고 있어야 인스펙터가 둘 열렸을 때 서로의 모드를 흔들지
	// 않는다(`EditorPropertyRow.h` 의 `property_layout_state` 주석).
	editor::widgets::property_layout_state m_layout{};

	bool m_openNewTagPopup{ false };
	bool m_openNewLayerPopup{ false };
	bool m_openFSMPopup{ false };
	bool m_openBTPopup{ false };

	// C# 스크립트의 노출 필드를 그린다.
	// 값 접근은 소스 제너레이터가 만든 인덱스 기반 접근자를 통한다(ClrHost 경유).
	void DrawManagedScripts(class ScriptComponent* script);

	void ImGuiDrawHelperGameObjectBaseInfo(Entity* gameObject);
	void ImGuiDrawHelperTransformComponent(Entity* gameObject);
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
	std::string     m_clipSearch;                 // 검색어
	std::vector<std::string> m_clipKeyCache;      // 캐시
};
