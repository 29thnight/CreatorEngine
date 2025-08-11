#pragma once
#include "Core.Mathf.h"
#include "imgui.h"

static inline bool NearEq(float a, float b) { return fabsf(a - b) < 1e-4f; }
static inline bool VecEq(const Mathf::Vector2& a, const Mathf::Vector2& b) {
	return NearEq(a.x, b.x) && NearEq(a.y, b.y);
}

// ���� ������ �� ĭ �׸��� (������ �ð�ȭ)
static inline bool DrawAnchorIconButton(const char* id, const Mathf::Vector2& aMin, const Mathf::Vector2& aMax,
	bool selected, ImVec2 size = ImVec2(28, 28))
{
	ImGui::PushID(id);
	const ImVec2 p = ImGui::GetCursorScreenPos();
	const ImRect r(p, ImVec2(p.x + size.x, p.y + size.y));
	bool pressed = ImGui::InvisibleButton("##btn", size);

	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImU32 colBase = IM_COL32(170, 170, 170, 255);
	const ImU32 colSel = IM_COL32(255, 200, 80, 255);
	dl->AddRect(r.Min, r.Max, selected ? colSel : colBase, 5.f, 0, selected ? 2.f : 1.f);

	// �»�(0,1)~����(1,0) ��ǥ ��ȯ
	auto X = [&](float nx) { return ImLerp(r.Min.x + 4, r.Max.x - 4, nx); };
	auto Y = [&](float ny) { return ImLerp(r.Max.y - 4, r.Min.y + 4, ny); }; // y�� ���� 1, �Ʒ��� 0

	bool stretchX = !NearEq(aMin.x, aMax.x);
	bool stretchY = !NearEq(aMin.y, aMax.y);

	if (stretchX) {
		float x1 = X(aMin.x), x2 = X(aMax.x);
		dl->AddLine(ImVec2(x1, r.Min.y + 6), ImVec2(x1, r.Max.y - 6), colBase, 1.0f);
		dl->AddLine(ImVec2(x2, r.Min.y + 6), ImVec2(x2, r.Max.y - 6), colBase, 1.0f);
	}
	if (stretchY) {
		float y1 = Y(aMin.y), y2 = Y(aMax.y);
		dl->AddLine(ImVec2(r.Min.x + 6, y1), ImVec2(r.Max.x - 6, y1), colBase, 1.0f);
		dl->AddLine(ImVec2(r.Min.x + 6, y2), ImVec2(r.Max.x - 6, y2), colBase, 1.0f);
	}
	// �� ������(= aMin==aMax) ǥ��
	if (!stretchX && !stretchY) {
		float x = X(aMin.x), y = Y(aMin.y);
		dl->AddLine(ImVec2(x, y - 5), ImVec2(x, y + 5), colBase, 1.0f);
		dl->AddLine(ImVec2(x - 5, y), ImVec2(x + 5, y), colBase, 1.0f);
	}

	ImGui::PopID();
	return pressed;
}

// Table �� �� ��ƿ
static inline bool DrawVec2Row(const char* label, Mathf::Vector2& v,
	float spd, float min, float max, const char* fmt = "%.3f")
{
	bool changed = false;
	ImGui::TableNextRow();

	// Label column
	ImGui::TableSetColumnIndex(0);
	ImGui::AlignTextToFramePadding(); // �Է� ������ ���� ����
	ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
	ImGui::TextUnformatted(label);
	ImGui::PopTextWrapPos();

	ImGui::PushID(label);

	// X column
	ImGui::TableSetColumnIndex(1);
	ImGui::SetNextItemWidth(-FLT_MIN); // �� ������ ����
	changed |= ImGui::DragFloat("##x", &v.x, spd, min, max, fmt);

	// Y column
	ImGui::TableSetColumnIndex(2);
	ImGui::SetNextItemWidth(-FLT_MIN); // �� ������ ����
	changed |= ImGui::DragFloat("##y", &v.y, spd, min, max, fmt);

	ImGui::PopID();
	return changed;
}

static inline bool DrawVec2RowAbs(const char* label, Mathf::Vector2& v,
	float spd = 1.f, const char* fmt = "%.3f")
{
	bool changed = false;
	ImGui::TableNextRow();

	// Label column
	ImGui::TableSetColumnIndex(0);
	ImGui::AlignTextToFramePadding();
	ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
	ImGui::TextUnformatted(label);
	ImGui::PopTextWrapPos();

	ImGui::PushID(label);

	// X column
	ImGui::TableSetColumnIndex(1);
	ImGui::SetNextItemWidth(-FLT_MIN);
	changed |= ImGui::DragFloat("##x", &v.x, spd, 0.0f, 0.0f, fmt); // min/max �̻��

	// Y column
	ImGui::TableSetColumnIndex(2);
	ImGui::SetNextItemWidth(-FLT_MIN);
	changed |= ImGui::DragFloat("##y", &v.y, spd, 0.0f, 0.0f, fmt);

	ImGui::PopID();
	return changed;
}

// ��ư/���� ���� ����, rect ������ �׸��� ����
static inline void DrawAnchorIconVisual(ImDrawList* dl, const ImRect& r,
	const Mathf::Vector2& aMin, const Mathf::Vector2& aMax,
	bool selected)
{
	const ImU32 colBase = IM_COL32(170, 170, 170, 255);
	const ImU32 colSel = IM_COL32(255, 200, 80, 255);

	dl->AddRect(r.Min, r.Max, selected ? colSel : colBase, 5.f, 0, selected ? 2.f : 1.f);

	auto X = [&](float nx) { return ImLerp(r.Min.x + 4, r.Max.x - 4, nx); };
	auto Y = [&](float ny) { return ImLerp(r.Max.y - 4, r.Min.y + 4, ny); }; // y: ���� 1, �Ʒ��� 0

	bool stretchX = fabsf(aMin.x - aMax.x) > 1e-4f;
	bool stretchY = fabsf(aMin.y - aMax.y) > 1e-4f;

	if (stretchX) {
		float x1 = X(aMin.x), x2 = X(aMax.x);
		dl->AddLine(ImVec2(x1, r.Min.y + 6), ImVec2(x1, r.Max.y - 6), colBase, 1.f);
		dl->AddLine(ImVec2(x2, r.Min.y + 6), ImVec2(x2, r.Max.y - 6), colBase, 1.f);
	}
	if (stretchY) {
		float y1 = Y(aMin.y), y2 = Y(aMax.y);
		dl->AddLine(ImVec2(r.Min.x + 6, y1), ImVec2(r.Max.x - 6, y1), colBase, 1.f);
		dl->AddLine(ImVec2(r.Min.x + 6, y2), ImVec2(r.Max.x - 6, y2), colBase, 1.f);
	}
	if (!stretchX && !stretchY) {
		float x = X(aMin.x), y = Y(aMin.y);
		dl->AddLine(ImVec2(x, y - 5), ImVec2(x, y + 5), colBase, 1.f);
		dl->AddLine(ImVec2(x - 5, y), ImVec2(x + 5, y), colBase, 1.f);
	}
}