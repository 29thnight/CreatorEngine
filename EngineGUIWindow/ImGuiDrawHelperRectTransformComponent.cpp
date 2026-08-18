#include "ExternUI.h"
#include "RectTransformComponent.h"
#include "TableAPIHelper.h"
#include "CustomCollapsingHeader.h"
#include "IconsFontAwesome6.h"
#include "fa.h"

// 프리셋 표는 RectTransformComponent가 소유한다. 여기에 사본을 두었더니 값이
// 갈라졌고, 양쪽 모두 스트레치 6종이 3종으로 뭉개져 있었다(분석 문서 F-6).
static const AnchorPresetEntry* Presets() { return GetAnchorPresetTable(); }
static int PresetCount() { return static_cast<int>(GetAnchorPresetCount()); }

static int FindPresetIndex(AnchorPreset p) {
	for (int i = 0; i < PresetCount(); ++i) if (Presets()[i].preset == p) return i;
	return -1;
}

static int FindCurrentPresetIndex(RectTransformComponent* rt) {
	auto a = rt->GetAnchorMin(), b = rt->GetAnchorMax();
	for (int i = 0; i < PresetCount(); ++i)
		if (VecEq(a, Presets()[i].anchorMin) && VecEq(b, Presets()[i].anchorMax)) return i;
	return -1;
}

// -------------------- ������ �˾�: Unity ��ġ --------------------
// 3x3 ����Ʈ ������ + ���ν�Ʈ��ġ 3 + ���ν�Ʈ��ġ 3 + ��ü��Ʈ��ġ 1
static void DrawAnchorPresetPopup(RectTransformComponent* rt)
{
	const float pad = 4.f;
	const ImVec2 cell(28, 28);

	int cur = FindCurrentPresetIndex(rt);

	auto drawBtn = [&](AnchorPreset ap) {
		int i = FindPresetIndex(ap);
		if (i < 0) return false;
		const auto& pr = Presets()[i];
		bool pressed = DrawAnchorIconButton(pr.label, pr.anchorMin, pr.anchorMax, i == cur, cell);
		if (pressed) rt->SetAnchorPreset(pr.preset);
		return pressed;
		};

	// 3x3 ����Ʈ
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

	// 세로 스트레치 3종 — 가로 위치(좌/중앙/우)만 다르다
	drawBtn(AnchorPreset::StretchLeft);   ImGui::SameLine(0, pad);
	drawBtn(AnchorPreset::StretchCenter); ImGui::SameLine(0, pad);
	drawBtn(AnchorPreset::StretchRight);

	// 가로 스트레치 3종 — 세로 위치(상/중/하)만 다르다
	ImGui::Dummy(ImVec2(1, 6));
	drawBtn(AnchorPreset::StretchTop);     ImGui::SameLine(0, pad);
	drawBtn(AnchorPreset::StretchMiddle);  ImGui::SameLine(0, pad);
	drawBtn(AnchorPreset::StretchBottom);

	// 양축 전체 스트레치
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

		// ��: ������ �˾� ��ư (Unityó��)
		ImGui::BeginGroup();
		ImGui::TextUnformatted("Anchors");
		{
			int curIndex = FindCurrentPresetIndex(rectTransformComponent);
			ImVec2 btnSize(36, 36);

			ImGui::PushID("CurrentPresetButton");
			bool pressed = ImGui::InvisibleButton("##currentPreset", btnSize);

			// ��ư�� ���� �簢��
			ImRect r(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

			// ���� �׸���: ��ħ ������ ������ ForegroundDrawList ���
			// ImDrawList* dl = ImGui::GetForegroundDrawList(); // �׻� �ֻ���
			ImDrawList* dl = ImGui::GetWindowDrawList();

			// ���(ȣ��/��Ƽ�� ����)
			ImU32 bgCol = ImGui::IsItemActive() ? IM_COL32(70, 70, 70, 255)
				: ImGui::IsItemHovered() ? IM_COL32(80, 80, 80, 255)
				: IM_COL32(60, 60, 60, 255);
			dl->AddRectFilled(r.Min, r.Max, bgCol, 4.f);

			// curIndex는 프리셋과 일치하지 않으면 -1이다. 이전 코드는 이 검사 전에
			// presets[curIndex]를 읽어, 앵커를 손으로 조절해 둔 상태에서 인스펙터를
			// 열면 배열 밖을 읽었다.
			if (curIndex >= 0) {
				const auto& pr = Presets()[curIndex];
				DrawAnchorIconVisual(dl, ImRect(r.Min + ImVec2(4, 4), r.Max - ImVec2(4, 4)),
					pr.anchorMin, pr.anchorMax, /*selected=*/false);
			}
			else {
				// Custom ����: ������ ����
				dl->AddLine(ImVec2((r.Min.x + r.Max.x) * 0.5f, r.Min.y + 4), ImVec2((r.Min.x + r.Max.x) * 0.5f, r.Max.y - 4), IM_COL32(200, 200, 200, 255), 1.f);
				dl->AddLine(ImVec2(r.Min.x + 4, (r.Min.y + r.Max.y) * 0.5f), ImVec2(r.Max.x - 4, (r.Min.y + r.Max.y) * 0.5f), IM_COL32(200, 200, 200, 255), 1.f);
			}

			if (pressed)
				ImGui::OpenPopup("AnchorPresetPopup");

			if (ImGui::BeginPopup("AnchorPresetPopup"))
			{
				DrawAnchorPresetPopup(rectTransformComponent); // ���� ������ ���
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

		// ��: �� ���� ���̺� (�� | X | Y)
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
				// 폴백 rect는 컴포넌트가 정한다 — 여기에 (0,0,W,H)를 따로 적어 두었더니
				// 캔버스 규약과 (W/2,H/2)만큼 어긋났다(PHASE 7-2).
				Mathf::Rect parentRect = RectTransformComponent::GetScreenRootRect();
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
	//		gameObject->Transform_().position = { 0, 0, 0, 1 };
	//		gameObject->Transform_().rotation = XMQuaternionIdentity();
	//		gameObject->Transform_().scale = { 1, 1, 1, 1 };
	//		gameObject->Transform_().SetDirty();
	//		gameObject->Transform_().UpdateLocalMatrix();
	//		ImGui::CloseCurrentPopup();
	//	}
	//	ImGui::EndPopup();
	//}
	//ImGui::PopStyleVar();
	//ImGui::PopStyleColor(2);
}