#include "Core.Minimal.h"
// Core.Minimal.h가 Reflection 사슬로 대신 끌어와 주던 것을 직접 든다.
#include <imgui.h>
#include "imgui_stdlib.h"
#include "AuthoringWriteNode.h"
#include "EditorPropertyRow.h"
#include "ExternUI.h"
#include "InspectorControl.h"
#include <sstream>
#include <unordered_set>

// 자산 Import Settings 본문 (PHASE 21 W2-I4).
//
// `.meta` 의 YAML 을 그대로 훑어 그린다. 키는 파일이 정하므로 컴파일 시 라벨 목록이
// 없다 — 라벨 열은 공통 최소 폭이고, 긴 키는 공통 줄이 잘라 그리고 tooltip 으로 준다.
// 예전 판은 키를 위젯 라벨에 붙여(`key##label`) 값 칸 오른쪽에 그렸고 폭은 ImGui 기본
// (창의 2/3)이라 좁은 폭에서 키가 작업 영역 밖으로 나갔다. 문자열은 256 바이트 버퍼에
// `strcpy_s` 로 복사해 그보다 긴 값이면 런타임 검사로 죽었다.

namespace
{
	const std::unordered_set<std::string> ignoredKeys = {
		"guid",
		"importSettings"
	};

	// 줄 배치 상태. 인스펙터는 한 스레드에서 그린다.
	editor::widgets::property_layout_state g_importLayout{};

	// 스칼라 한 줄. 표기로 형을 고른다 — bool, 정수(64 비트), 실수, 나머지는 문자열.
	void DrawScalar(const editor::widgets::property_sheet& sheet, const char* label,
		const Authoring::WriteNode& node, const std::string& text)
	{
		if (text == "true" || text == "false")
		{
			bool value = (text == "true");
			sheet.line(label);
			if (ImGui::Checkbox("##Value", &value))
				node.SetScalar(value);
			return;
		}

		ImGui::SetNextItemWidth(sheet.line(label));
		{
			std::istringstream stream(text);
			long long value = 0;
			if ((stream >> value) && stream.eof())
			{
				if (ImGui::InputScalar("##Value", ImGuiDataType_S64, &value))
					node.SetScalar(value);
				return;
			}
		}
		{
			std::istringstream stream(text);
			float value = 0.f;
			if ((stream >> value) && stream.eof())
			{
				if (ImGui::InputFloat("##Value", &value))
					node.SetScalar(value);
				return;
			}
		}
		std::string value = text;
		if (ImGui::InputText("##Value", &value))
			node.SetScalar(value);
		if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) ImGui::SetTooltip("%s", text.c_str());
	}

	// 맵·배열 하나를 접힘 머리로 연다. 검사가 `editor.inspector expand on` 을 주면 펼친다.
	bool BeginContainer(const char* label)
	{
		if (editor::windows::inspector_expand_all())
			ImGui::SetNextItemOpen(true, ImGuiCond_Always);
		return ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_SpanAvailWidth);
	}

	void DrawEntry(const char* label, const Authoring::WriteNode& node, const Authoring::ReadNode& read)
	{
		if (read.IsMap() || read.IsSequence())
		{
			if (BeginContainer(label))
			{
				DrawYamlNodeEditor(node, label);
				ImGui::TreePop();
			}
			return;
		}
		if (!read.IsScalar()) return;

		// 들여쓰기가 깊이마다 폭을 줄이므로 배치는 그 자리에서 잰다.
		const editor::widgets::property_sheet sheet(g_importLayout, {});
		DrawScalar(sheet, label, node, read.AsString());
	}
}

void DrawYamlNodeEditor(Authoring::WriteNode node, const std::string& label)
{
	(void)label;
	const Authoring::ReadNode read = node.Read();
	if (!read || read.IsNull()) return;

	if (read.IsMap())
	{
		for (const Authoring::MapEntry pair : read.Map())
		{
			const std::string key = pair.key.AsString();
			if (ignoredKeys.count(key)) continue;

			ImGui::PushID(key.c_str());
			DrawEntry(key.c_str(), node.Child(key), pair.value);
			ImGui::PopID();
		}
	}
	else if (read.IsSequence())
	{
		for (std::size_t index = 0; index < node.Size(); ++index)
		{
			const Authoring::WriteNode element = node.At(index);
			const std::string elementLabel = "[" + std::to_string(index) + "]";
			ImGui::PushID(static_cast<int>(index));
			DrawEntry(elementLabel.c_str(), element, element.Read());
			ImGui::PopID();
		}
	}
}
