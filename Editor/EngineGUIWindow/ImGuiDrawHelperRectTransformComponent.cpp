#include "ExternUI.h"
#include "RectTransformComponent.h"
#include "EditorPropertyRow.h"
#include "EditorSectionHeader.h"
#include "EditorIcons.h"

#include <cmath>
#include <cstdio>

// 프리셋 표는 RectTransformComponent가 소유한다. 여기에 사본을 두었더니 값이
// 갈라졌고, 양쪽 모두 스트레치 6종이 3종으로 뭉개져 있었다(분석 문서 F-6).
static const AnchorPresetEntry* Presets() { return GetAnchorPresetTable(); }
static int PresetCount() { return static_cast<int>(GetAnchorPresetCount()); }

namespace
{
	// ── 앵커 아이콘은 여기가 유일한 소비자다 ──────────────────────────────
	//
	// 이 넷은 `ImGuiHelper/TableAPIHelper.h` 에 있었다. 그 파일의 이름은 범용
	// 표 헬퍼를 가리켰지만 내보내는 다섯이 전부 RectTransform 의 앵커·vec2
	// 행이었고, 소비자도 이 파일 하나였다(결정표 §2.2). 행 규약은
	// `EditorPropertyRow` 가 승계했고, 남은 앵커 그림은 범용이 아니므로
	// 쓰는 자리로 내렸다. 헤더가 사라지면서 그것을 들이기만 하고 한 번도
	// 부르지 않던 두 파일(`InspectorWindow.cpp`, `ImGuiDrawHelperTerrainComponent.cpp`)의
	// 죽은 include 도 함께 없어졌다.
	//
	// ── 내리면서 고친 것: 뒤집힌 Y 축 ─────────────────────────────────────
	//
	// 같은 그림을 그리는 함수가 **두 벌**이었고 세로 축이 서로 반대였다.
	// 버튼 쪽은 `ImLerp(Min.y, Max.y, ny)`(ny=0 이 위), 그리기 전용 쪽은
	// `ImLerp(Max.y, Min.y, ny)`(ny=0 이 아래)였다. 앵커 표는 y-down 이다 —
	// `TopLeft={0,0}`, `BottomLeft={0,1}`(`RectTransformComponent.h:16`). 즉
	// 그리기 전용 쪽이 틀렸고, 인스펙터의 "현재 프리셋" 버튼은 고른 것과
	// 위아래가 뒤집힌 그림을 보이고 있었다. 팝업 안의 3x3 은 버튼 쪽을 쓰므로
	// 멀쩡했다. 그래서 눈에 띄기 어려웠다 — 고른 칸과 보이는 그림이 다른데
	// 둘을 나란히 볼 일이 없다.
	//
	// 고친 방식은 그림 함수를 하나로 만든 것이다. 축이 한 자리에만 있으면
	// 두 벌이 갈라질 수 없다.

	bool NearEq(float a, float b) { return fabsf(a - b) < 1e-4f; }

	constexpr ImU32 kAnchorIconBase = IM_COL32(170, 170, 170, 255);
	constexpr ImU32 kAnchorIconSelected = IM_COL32(255, 200, 80, 255);

	// rect 위에만 그린다. 자리를 잡지 않으므로 호출자가 먼저 잡아 둔다.
	void DrawAnchorIcon(ImDrawList* dl, const ImRect& r,
		const math::vector2& aMin, const math::vector2& aMax, bool selected)
	{
		const ImU32 frame = selected ? kAnchorIconSelected : kAnchorIconBase;
		dl->AddRect(r.Min, r.Max, frame, 5.f, selected ? 2.f : 1.f, 0);

		// 앵커 좌표는 y-down 이다: 0 이 위, 1 이 아래.
		auto X = [&](float nx) { return ImLerp(r.Min.x + 4, r.Max.x - 4, nx); };
		auto Y = [&](float ny) { return ImLerp(r.Min.y + 4, r.Max.y - 4, ny); };

		const bool stretchX = !NearEq(aMin.x, aMax.x);
		const bool stretchY = !NearEq(aMin.y, aMax.y);

		if (stretchX) {
			const float x1 = X(aMin.x), x2 = X(aMax.x);
			dl->AddLine(ImVec2(x1, r.Min.y + 6), ImVec2(x1, r.Max.y - 6), kAnchorIconBase, 1.f);
			dl->AddLine(ImVec2(x2, r.Min.y + 6), ImVec2(x2, r.Max.y - 6), kAnchorIconBase, 1.f);
		}
		if (stretchY) {
			const float y1 = Y(aMin.y), y2 = Y(aMax.y);
			dl->AddLine(ImVec2(r.Min.x + 6, y1), ImVec2(r.Max.x - 6, y1), kAnchorIconBase, 1.f);
			dl->AddLine(ImVec2(r.Min.x + 6, y2), ImVec2(r.Max.x - 6, y2), kAnchorIconBase, 1.f);
		}
		// 점 프리셋(= aMin==aMax)은 십자로 표시한다.
		if (!stretchX && !stretchY) {
			const float x = X(aMin.x), y = Y(aMin.y);
			dl->AddLine(ImVec2(x, y - 5), ImVec2(x, y + 5), kAnchorIconBase, 1.f);
			dl->AddLine(ImVec2(x - 5, y), ImVec2(x + 5, y), kAnchorIconBase, 1.f);
		}
	}

	// 작은 아이콘 한 칸을 누를 수 있게 그린다 (프리셋 시각화).
	bool DrawAnchorIconButton(const char* id, const math::vector2& aMin, const math::vector2& aMax,
		bool selected, ImVec2 size)
	{
		ImGui::PushID(id);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const ImRect r(p, ImVec2(p.x + size.x, p.y + size.y));
		const bool pressed = ImGui::InvisibleButton("##btn", size);

		DrawAnchorIcon(ImGui::GetWindowDrawList(), r, aMin, aMax, selected);

		ImGui::PopID();
		return pressed;
	}

	bool VecEq(const math::vector2& a, const math::vector2& b)
	{
		return NearEq(a.x, b.x) && NearEq(a.y, b.y);
	}

	// vec2 한 줄 (W2-I2). 라벨과 값 열은 공통 줄이 놓고, 두 칸이 값 열을 나눠 갖는다.
	// 예전에는 앵커 버튼 오른쪽의 3 열 표 한 행이었다 — 표 전체가 버튼 폭만큼 밀려
	// 좁은 폭에서 넘쳤다. 값 열이 축 둘을 담지 못하면
	// 축마다 한 줄을 쓴다. ID 는 표 시절 그대로 라벨 아래 `##f0`·`##f1` 이다.
	bool DrawVec2Row(const editor::widgets::property_sheet& sheet, const char* label,
		math::vector2& v, float speed, float min, float max)
	{
		const float width = sheet.line(label);
		// 공통 배치의 `axis_stacked` 는 축 셋을 기준으로 판정해 두 칸에는 너무 이르다. 같은 최소치(`axis_need`)를
		// 두 칸에 대어 본다 — 폭에서만 유도하므로 값이 바뀌어도 흔들리지 않는다.
		const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
		const bool stacked = (width - gap) * 0.5f < sheet.metrics().axis_need;
		const float field = stacked ? width : ImMax((width - gap) * 0.5f, 1.f);
		bool changed = false;
		ImGui::PushID(label);
		ImGui::BeginGroup();
		for (int index = 0; index < 2; ++index)
		{
			if (index > 0 && !stacked) ImGui::SameLine(0.f, gap);
			ImGui::SetNextItemWidth(field);
			changed |= editor::widgets::drag_property_float(
				editor::widgets::property_row_field_id(index), &v.x + index, speed, min, max);
		}
		ImGui::EndGroup();
		ImGui::PopID();
		return changed;
	}
}

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

// -------------------- 프리셋 팝업: Unity 배치 --------------------
// 3x3 포인트 프리셋 + 가로스트레치 3 + 세로스트레치 3 + 전체스트레치 1
static void DrawAnchorPresetPopup(RectTransformComponent* rt)
{
	// 칸 크기는 프레임 높이를 따른다 — 논리 28 px 을 그대로 두면 배율 2 에서 아이콘만 작아진다.
	const float pad = ImGui::GetStyle().ItemInnerSpacing.x;
	const float side = ImGui::GetFrameHeight() * 1.4f;
	const ImVec2 cell(side, side);
	const ImVec2 gapBelow(0.f, ImGui::GetStyle().ItemSpacing.y);

	int cur = FindCurrentPresetIndex(rt);

	auto drawBtn = [&](AnchorPreset ap) {
		int i = FindPresetIndex(ap);
		if (i < 0) return false;
		const auto& pr = Presets()[i];
		bool pressed = DrawAnchorIconButton(pr.label, pr.anchorMin, pr.anchorMax, i == cur, cell);
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

	ImGui::Dummy(gapBelow);
	ImGui::Separator();
	ImGui::Dummy(gapBelow);

	// 세로 스트레치 3종 — 가로 위치(좌/중앙/우)만 다르다
	drawBtn(AnchorPreset::StretchLeft);   ImGui::SameLine(0, pad);
	drawBtn(AnchorPreset::StretchCenter); ImGui::SameLine(0, pad);
	drawBtn(AnchorPreset::StretchRight);

	// 가로 스트레치 3종 — 세로 위치(상/중/하)만 다르다
	ImGui::Dummy(gapBelow);
	drawBtn(AnchorPreset::StretchTop);     ImGui::SameLine(0, pad);
	drawBtn(AnchorPreset::StretchMiddle);  ImGui::SameLine(0, pad);
	drawBtn(AnchorPreset::StretchBottom);

	// 양축 전체 스트레치
	ImGui::Dummy(gapBelow);
	drawBtn(AnchorPreset::StretchAll);
}

void ImGuiDrawHelperRectTransformComponent(RectTransformComponent* rectTransformComponent)
{
	if (!rectTransformComponent) return;

	// 메뉴 버튼은 그대로 그린다 — 모습을 바꾸지 않기 위해서다. 누른 결과를
	// 받는 쪽은 이 함수 끝의 주석 처리된 TransformMenu 블록이라, 이 변경
	// 전에도 소비자가 없었다.
	editor::widgets::section_header_request header{};
	header.label = "RectTransform";
	header.menu_icon = EditorIcon::More;
	if (editor::widgets::draw_section_header(header).open)
	{
		auto anchorMin = rectTransformComponent->GetAnchorMin();
		auto anchorMax = rectTransformComponent->GetAnchorMax();
		auto anchoredPos = rectTransformComponent->GetAnchoredPosition();
		auto sizeDelta = rectTransformComponent->GetSizeDelta();
		auto pivot = rectTransformComponent->GetPivot();

		// W2-I2: 모든 줄이 공통 배치를 쓴다. 예전에는 왼쪽에 앵커 버튼 무리(36 px 고정),
		// 세로 구분선, 오른쪽에 3 열 표를 나란히 두어, 표가 버튼 폭만큼 밀린 채
		// 줄어들지 못하고 240 폭에서 122 px 넘쳤다(배율 2.25). 버튼도 한 줄의 값으로
		// 세우면 라벨 열이 다른 컴포넌트와 같은 x 에 서고, 좁으면 공통 배치가 값을
		// 라벨 아래로 내린다.
		//
		// 상태가 함수 지역 정적인 이유: 이 드로어는 자유 함수라 창 객체가 없다.
		// 인스펙터가 하나뿐이라 성립하고, 둘이 되면 호출자가 소유해야 한다
		// (`EditorPropertyRow.h` 의 `property_layout_state` 주석).
		static editor::widgets::property_layout_state rectLayoutState{};
		const editor::widgets::property_sheet sheet(rectLayoutState,
			{ "Anchors", "Anchor Min", "Anchor Max", "Pos", "Width/Height", "Pivot", "World Rect" });

		{
			const float valueWidth = sheet.line("Anchors");
			const float side = ImMin(ImGui::GetFrameHeight() * 1.6f, valueWidth);
			const int curIndex = FindCurrentPresetIndex(rectTransformComponent);

			ImGui::PushID("CurrentPresetButton");
			const bool pressed = ImGui::InvisibleButton("##currentPreset", ImVec2(side, side));
			const ImRect r(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
			ImDrawList* dl = ImGui::GetWindowDrawList();

			// 배경(호버/액티브 반응)
			ImU32 bgCol = ImGui::IsItemActive() ? IM_COL32(70, 70, 70, 255)
				: ImGui::IsItemHovered() ? IM_COL32(80, 80, 80, 255)
				: IM_COL32(60, 60, 60, 255);
			dl->AddRectFilled(r.Min, r.Max, bgCol, 4.f);

			// curIndex는 프리셋과 일치하지 않으면 -1이다. 이전 코드는 이 검사 전에
			// presets[curIndex]를 읽어, 앵커를 손으로 조절해 둔 상태에서 인스펙터를
			// 열면 배열 밖을 읽었다.
			const ImVec2 inset(side * 0.11f, side * 0.11f);
			if (curIndex >= 0) {
				const auto& pr = Presets()[curIndex];
				DrawAnchorIcon(dl, ImRect(r.Min + inset, r.Max - inset),
					pr.anchorMin, pr.anchorMax, /*selected=*/false);
			}
			else {
				// Custom 상태: 간단한 십자
				const ImVec2 c = r.GetCenter();
				dl->AddLine(ImVec2(c.x, r.Min.y + inset.y), ImVec2(c.x, r.Max.y - inset.y), IM_COL32(200, 200, 200, 255), 1.f);
				dl->AddLine(ImVec2(r.Min.x + inset.x, c.y), ImVec2(r.Max.x - inset.x, c.y), IM_COL32(200, 200, 200, 255), 1.f);
			}

			if (pressed)
				ImGui::OpenPopup("AnchorPresetPopup");

			// 예전에는 같은 팝업을 ID 안과 밖에서 두 번 열었다. 여는 자리와 같은 ID 에서 한 번만 연다.
			if (ImGui::BeginPopup("AnchorPresetPopup"))
			{
				DrawAnchorPresetPopup(rectTransformComponent);
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}

		bool anchorsChanged = false;
		bool pivotChanged = false;

		if (DrawVec2Row(sheet, "Anchor Min", anchorMin, 0.01f, 0.f, 1.f))
			anchorsChanged = true;
		if (DrawVec2Row(sheet, "Anchor Max", anchorMax, 0.01f, 0.f, 1.f))
			anchorsChanged = true;

		// 제한 없는 줄은 min/max 를 같은 값으로 둔다 — 원본
		// `DrawVec2RowAbs` 가 `0,0` 을 넘겨 끄던 것과 같은 규약이다.
		if (DrawVec2Row(sheet, "Pos", anchoredPos, 1.f, 0.f, 0.f))
			rectTransformComponent->SetAnchoredPosition(anchoredPos);

		if (DrawVec2Row(sheet, "Width/Height", sizeDelta, 1.f, 0.f, 0.f))
			rectTransformComponent->SetSizeDelta(sizeDelta);

		if (DrawVec2Row(sheet, "Pivot", pivot, 0.01f, 0.f, 1.f))
			pivotChanged = true;

		if (anchorsChanged || pivotChanged)
		{
			// 폴백 rect는 컴포넌트가 정한다 — 여기에 (0,0,W,H)를 따로 적어 두었더니
			// 캔버스 규약과 (W/2,H/2)만큼 어긋났다(PHASE 7-2).
			math::rect parentRect = RectTransformComponent::GetScreenRootRect();
			if (auto* owner = rectTransformComponent->GetOwner(); owner)
			{
				if (Entity::IsValidIndex(owner->GetParentIndex()))
				{
					if (auto* parentObj = owner->OwnerSceneFindIndex(owner->GetParentIndex()))
					{
						if (auto* parentRT = parentObj->GetComponent<RectTransformComponent>())
							parentRect = parentRT->GetWorldRect();
					}
				}
			}
			rectTransformComponent->SetAnchorsPivotKeepWorld(anchorMin, anchorMax, pivot, parentRect);
		}

		// 읽기 전용 값. 예전 `Text` 한 줄은 자르지 않아 좁은 폭에서 그대로 넘쳤다 — 입력칸
		// 틀이 잘라 그리고, 전체는 tooltip 으로 준다.
		const auto& wr = rectTransformComponent->GetWorldRect();
		char worldRect[96];
		snprintf(worldRect, sizeof(worldRect), "%.1f  %.1f  %.1f  %.1f", wr.x, wr.y, wr.width, wr.height);
		ImGui::SetNextItemWidth(sheet.line("World Rect"));
		ImGui::InputText("##WorldRect", worldRect, sizeof(worldRect), ImGuiInputTextFlags_ReadOnly);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("x y w h: %s", worldRect);
	}
	//if (menuClicked) {
	//	ImGui::OpenPopup("TransformMenu");
	//	menuClicked = false;
	//}

	//if (ImGui::BeginPopup("TransformMenu"))
	//{
	//	if (ImGui::MenuItem("Reset Transform"))
	//	{
	//		gameObject->Transform_().position = { 0, 0, 0, 1 };
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
