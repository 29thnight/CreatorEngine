#include "ExternUI.h"
#include "DeviceState.h"
#include "RectTransformComponent.h"
#include "TableAPIHelper.h"
#include "CustomCollapsingHeader.h"
#include "IconsFontAwesome6.h"
#include "fa.h"

struct PresetInfo {
	AnchorPreset preset;
	Mathf::Vector2 anchorMin;
	Mathf::Vector2 anchorMax;
	const char* name;
};

static constexpr std::array<PresetInfo, 16> presets{ {
		{ AnchorPreset::TopLeft,      {0.f,  0.f},	{0.f,  0.f},	"TopLeft"		},
		{ AnchorPreset::TopCenter,    {0.5f, 0.f},	{0.5f, 0.f},	"TopCenter"		},
		{ AnchorPreset::TopRight,     {1.f,  0.f},	{1.f,  0.f},	"TopRight"		},
		{ AnchorPreset::MiddleLeft,   {0.f,  0.5f},	{0.f,  0.5f},	"MiddleLeft"	},
		{ AnchorPreset::MiddleCenter, {0.5f, 0.5f}, {0.5f, 0.5f},	"MiddleCenter"	},
		{ AnchorPreset::MiddleRight,  {1.f,  0.5f},	{1.f,  0.5f},	"MiddleRight"	},
		{ AnchorPreset::BottomLeft,   {0.f,  1.f},	{0.f,  1.f},	"BottomLeft"	},
		{ AnchorPreset::BottomCenter, {0.5f, 1.f},	{0.5f, 1.f},	"BottomCenter"	},
		{ AnchorPreset::BottomRight,  {1.f,  1.f},	{1.f,  1.f},	"BottomRight"	},
		{ AnchorPreset::StretchLeft,  {0.f,  0.5f},	{1.f,  0.5f},	"StretchLeft"	},
		{ AnchorPreset::StretchCenter,{0.f,  0.5f},	{1.f,  0.5f},	"StretchCenter"	},
		{ AnchorPreset::StretchRight, {0.f,  0.5f},	{1.f,  0.5f},	"StretchRight"	},
		{ AnchorPreset::StretchTop,   {0.5f, 0.f},	{0.5f, 1.f},	"StretchTop"	},
		{ AnchorPreset::StretchMiddle,{0.5f, 0.f},	{0.5f, 1.f},	"StretchMiddle"	},
		{ AnchorPreset::StretchBottom,{0.5f, 0.f},	{0.5f, 1.f},	"StretchBottom"	},
		{ AnchorPreset::StretchAll,   {0.f,  0.f},	{1.f,  1.f},	"StretchAll"	},
} };

static int FindPresetIndex(AnchorPreset p) {
	for (int i = 0; i < (int)presets.size(); ++i) if (presets[i].preset == p) return i;
	return -1;
}

static int FindCurrentPresetIndex(RectTransformComponent* rt) {
	auto a = rt->GetAnchorMin(), b = rt->GetAnchorMax();
	for (int i = 0; i < (int)presets.size(); ++i)
		if (VecEq(a, presets[i].anchorMin) && VecEq(b, presets[i].anchorMax)) return i;
	return -1;
}

// -------------------- 프리셋 팝업: Unity 배치 --------------------
// 3x3 포인트 프리셋 + 가로스트레치 3 + 세로스트레치 3 + 전체스트레치 1
static void DrawAnchorPresetPopup(RectTransformComponent* rt)
{
	const float pad = 4.f;
	const ImVec2 cell(28, 28);

	int cur = FindCurrentPresetIndex(rt);

	auto drawBtn = [&](AnchorPreset ap) {
		int i = FindPresetIndex(ap);
		const auto& pr = presets[i];
		bool pressed = DrawAnchorIconButton(pr.name, pr.anchorMin, pr.anchorMax, i == cur, cell);
		if (pressed) rt->SetAnchorPreset(pr.preset);
		return pressed;
		};

	// 3x3 포인트
	{
		// TL, TC, TR
		drawBtn(AnchorPreset::TopLeft);   ImGui::SameLine(0, pad);
		drawBtn(AnchorPreset::TopCenter); ImGui::SameLine(0, pad);
		drawBtn(AnchorPreset::TopRight);
		// ML, MC, MR
		drawBtn(AnchorPreset::MiddleLeft); ImGui::SameLine(0, pad);
		drawBtn(AnchorPreset::MiddleCenter); ImGui::SameLine(0, pad);
		drawBtn(AnchorPreset::MiddleRight);
		// BL, BC, BR
		drawBtn(AnchorPreset::BottomLeft); ImGui::SameLine(0, pad);
		drawBtn(AnchorPreset::BottomCenter); ImGui::SameLine(0, pad);
		drawBtn(AnchorPreset::BottomRight);
	}

	ImGui::Dummy(ImVec2(1, 6));
	ImGui::Separator();
	ImGui::Dummy(ImVec2(1, 6));

	// 가로 스트레치 3
	drawBtn(AnchorPreset::StretchLeft);   ImGui::SameLine(0, pad);
	drawBtn(AnchorPreset::StretchCenter); ImGui::SameLine(0, pad);
	drawBtn(AnchorPreset::StretchRight);

	// 세로 스트레치 3
	ImGui::Dummy(ImVec2(1, 6));
	drawBtn(AnchorPreset::StretchTop);     ImGui::SameLine(0, pad);
	drawBtn(AnchorPreset::StretchMiddle);  ImGui::SameLine(0, pad);
	drawBtn(AnchorPreset::StretchBottom);

	// 전체 스트레치
	ImGui::Dummy(ImVec2(1, 6));
	drawBtn(AnchorPreset::StretchAll);
}

void ImGuiDrawHelperRectTransformComponent(RectTransformComponent* rectTransformComponent)
{
	if (!rectTransformComponent) return;

	bool menuClicked = false;
	if (ImGui::DrawCollapsingHeaderWithButton("RectTransform", ImGuiTreeNodeFlags_DefaultOpen, ICON_FA_BARS, &menuClicked))
	{
		auto anchorMin = rectTransformComponent->GetAnchorMin();
		auto anchorMax = rectTransformComponent->GetAnchorMax();
		auto anchoredPos = rectTransformComponent->GetAnchoredPosition();
		auto sizeDelta = rectTransformComponent->GetSizeDelta();
		auto pivot = rectTransformComponent->GetPivot();

		// 좌: 프리셋 팝업 버튼 (Unity처럼)
		ImGui::BeginGroup();
		ImGui::TextUnformatted("Anchors");
		{
			int curIndex = FindCurrentPresetIndex(rectTransformComponent);
			ImVec2 btnSize(36, 36);

			ImGui::PushID("CurrentPresetButton");
			bool pressed = ImGui::InvisibleButton("##currentPreset", btnSize);

			// 버튼의 실제 사각형
			ImRect r(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

			// 위에 그리기: 겹침 문제가 있으면 ForegroundDrawList 사용
			// ImDrawList* dl = ImGui::GetForegroundDrawList(); // 항상 최상위
			ImDrawList* dl = ImGui::GetWindowDrawList();

			// 배경(호버/액티브 반응)
			ImU32 bgCol = ImGui::IsItemActive() ? IM_COL32(70, 70, 70, 255)
				: ImGui::IsItemHovered() ? IM_COL32(80, 80, 80, 255)
				: IM_COL32(60, 60, 60, 255);
			dl->AddRectFilled(r.Min, r.Max, bgCol, 4.f);

			Mathf::Vector2 calcMin = presets[curIndex].anchorMin;
			Mathf::Vector2 calcMax = presets[curIndex].anchorMax;

			if (curIndex >= 0) {
				const auto& pr = presets[curIndex];
				// 아이콘 비주얼만 그리기 (selected=false: 이미 현재 버튼이라 테두리 과도 방지)
				DrawAnchorIconVisual(dl, ImRect(r.Min + ImVec2(4, 4), r.Max - ImVec2(4, 4)),
					calcMin, calcMax, /*selected=*/false);
			}
			else {
				// Custom 상태: 간단한 십자
				dl->AddLine(ImVec2((r.Min.x + r.Max.x) * 0.5f, r.Min.y + 4), ImVec2((r.Min.x + r.Max.x) * 0.5f, r.Max.y - 4), IM_COL32(200, 200, 200, 255), 1.f);
				dl->AddLine(ImVec2(r.Min.x + 4, (r.Min.y + r.Max.y) * 0.5f), ImVec2(r.Max.x - 4, (r.Min.y + r.Max.y) * 0.5f), IM_COL32(200, 200, 200, 255), 1.f);
			}

			if (pressed)
				ImGui::OpenPopup("AnchorPresetPopup");

			if (ImGui::BeginPopup("AnchorPresetPopup"))
			{
				DrawAnchorPresetPopup(rectTransformComponent); // 기존 프리셋 목록
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}

		if (ImGui::BeginPopup("AnchorPresetPopup"))
		{
			DrawAnchorPresetPopup(rectTransformComponent);
			ImGui::EndPopup();
		}
		ImGui::EndGroup();

		ImGui::SameLine();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		ImGui::SameLine();

		// 우: 값 편집 테이블 (라벨 | X | Y)
		if (ImGui::BeginTable("RectTransformTable", 3,
			ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit, ImVec2(-1, 0)))
		{
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthFixed, 90.f);
			ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthFixed, 90.f);
			ImGui::TableHeadersRow();

			bool anchorsChanged = false;
			bool pivotChanged = false;

			if (DrawVec2Row("Anchor Min", anchorMin, 0.01f, 0.f, 1.f))
				anchorsChanged = true;
			if (DrawVec2Row("Anchor Max", anchorMax, 0.01f, 0.f, 1.f))
				anchorsChanged = true;

			if (DrawVec2RowAbs("Pos", anchoredPos, 1.f))
				rectTransformComponent->SetAnchoredPosition(anchoredPos);

			if (DrawVec2RowAbs("Width/Height", sizeDelta, 1.f))
				rectTransformComponent->SetSizeDelta(sizeDelta);

			if (DrawVec2Row("Pivot", pivot, 0.01f, 0.f, 1.f))
				pivotChanged = true;

			if (anchorsChanged || pivotChanged)
			{
				Mathf::Rect parentRect{ 0.f, 0.f, DirectX11::DeviceStates->g_ClientRect.width, DirectX11::DeviceStates->g_ClientRect.height };
				if (auto* owner = rectTransformComponent->GetOwner(); owner)
				{
					if (GameObject::IsValidIndex(owner->m_parentIndex))
					{
						if (auto* parentObj = GameObject::FindIndex(owner->m_parentIndex))
						{
							if (auto* parentRT = parentObj->GetComponent<RectTransformComponent>())
								parentRect = parentRT->GetWorldRect();
						}
					}
				}
				rectTransformComponent->SetAnchorsPivotKeepWorld(anchorMin, anchorMax, pivot, parentRect);
			}

			ImGui::EndTable();
		}

		const auto& wr = rectTransformComponent->GetWorldRect();
		ImGui::Spacing();
		ImGui::Text("World Rect (x y w h): %.1f  %.1f  %.1f  %.1f", wr.x, wr.y, wr.width, wr.height);
	}
	//if (menuClicked) {
	//	ImGui::OpenPopup("TransformMenu");
	//	menuClicked = false;
	//}

	//ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.95f, 0.95f, 0.95f, 1.0f));
	//ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
	//ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 5.0f);
	//if (ImGui::BeginPopup("TransformMenu"))
	//{
	//	if (ImGui::MenuItem("Reset Transform"))
	//	{
	//		gameObject->m_transform.position = { 0, 0, 0, 1 };
	//		gameObject->m_transform.rotation = XMQuaternionIdentity();
	//		gameObject->m_transform.scale = { 1, 1, 1, 1 };
	//		gameObject->m_transform.SetDirty();
	//		gameObject->m_transform.UpdateLocalMatrix();
	//		ImGui::CloseCurrentPopup();
	//	}
	//	ImGui::EndPopup();
	//}
	//ImGui::PopStyleVar();
	//ImGui::PopStyleColor(2);
}