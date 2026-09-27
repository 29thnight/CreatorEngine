// 애니메이터 인스펙터 본문 (PHASE 21 M4 3b 뒤).
//
// 편집 창 셋(Event · Animation Controllers · AvatarMask)의 본문은 여기 있지
// 않다 — 셸이 프레임을 소유하므로 AnimatorEditorWindows.cpp로 갔다. 여기
// 남은 것은 인스펙터 안에 그리는 접힘머리와, 그 창들을 여는 단추뿐이다.

#include "EditorObjectOperations.h"
#include "ReflectionImGuiHelper.h"
#include "ReflectionTypedDraw.h"
#include "ClrHost.h"
#include "Animator.h"
#include "NodeEditor.h"
#include "AnimationController.h"
#include "EditorIcons.h"
#include "EditorTheme.h"
#include "ExternUI.h"
// DataSystems가 여기 있다. 유니티 빌드에서는 앞선 파일이 공급했다.
#include "DataSystem.h"
#include "AnimatorEditorWindows.h"
#include "EditorWindowNames.h"
#include "EditorWindowRegistry.h"
#include "EditorPropertyRow.h"
#include "imgui_stdlib.h"
#include "SceneManager.h"
#include <algorithm>
#include <climits>
#include <cmath>

namespace
{
    // 창 하나를 여닫는 단추 하나. 옛 코드가 `bool = !bool` 로 쓰던 자리다.
    void ToggleAnimatorWindow(const char* window)
    {
        if (::editor::is_window_open(window)) ::editor::close_window(window);
        else                                  ::editor::open_window(window);
    }

    // 전용 드로어의 줄 배치 상태 (PHASE 21 W2-I4). 인스펙터는 한 스레드에서 그린다.
    editor::widgets::property_layout_state g_animatorLayout{};

    void DrawPreviewPlaybackControls(AnimInstance& preview, bool enabled)
    {
        using editor::ThemeColor;
        const float scale = editor::ThemePixels(1.f);
        const float height = ImGui::GetFrameHeight();
        const float groupWidth = height * 3.f;
        const ImVec2 boxMin = ImGui::GetCursorScreenPos();
        const ImVec2 boxMax{ boxMin.x + groupWidth, boxMin.y + height };
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(boxMin, boxMax,
            ImGui::GetColorU32(editor::ThemeColorValue(ThemeColor::Chrome)), 3.f * scale);
        draw->AddRect(boxMin, boxMax,
            ImGui::GetColorU32(editor::ThemeColorValue(ThemeColor::Border)), 3.f * scale);

        const bool running = preview.editorPreviewPlaying || preview.editorPreviewPaused;
        const bool paused = preview.editorPreviewPaused;
        ImGui::PushFont(nullptr, editor::EditorThemeTokens::IconFontSize);
        ImGui::PushID("PreviewClipPlayControls");
        ImGui::BeginGroup();
        for (int index = 0; index < 2; ++index)
        {
            const bool buttonEnabled = enabled && (index == 0 || running);
            const bool active = index == 0 ? running : paused;
            const char* icon = index == 0 ? (running ? EditorIcon::Stop : EditorIcon::Play)
                : (paused ? EditorIcon::Play : EditorIcon::Pause);
            const char* tip = index == 0 ? (running ? "Stop clip preview" : "Play clip preview")
                : (paused ? "Resume clip preview" : "Pause clip preview");
            const float width = groupWidth * .5f - 2.f * scale;
            const ImVec2 p{ boxMin.x + scale + index * groupWidth * .5f,
                boxMin.y + scale };
            ImGui::SetCursorScreenPos(p);
            ImGui::BeginDisabled(!buttonEnabled);
            const bool pressed = ImGui::InvisibleButton(index == 0 ? "PlayStop" : "PauseResume",
                { width, height - 2.f * scale }, ImGuiButtonFlags_EnableNav);
            const bool hovered = ImGui::IsItemHovered();
            if (hovered || ImGui::IsItemActive())
                draw->AddRectFilled(p, { p.x + width, p.y + height - 2.f * scale },
                    ImGui::GetColorU32(editor::ThemeColorValue(
                        ImGui::IsItemActive() ? ThemeColor::Selection : ThemeColor::PanelRaised)),
                    2.f * scale);
            if (ImGui::IsItemFocused())
                draw->AddRect(p, { p.x + width, p.y + height - 2.f * scale },
                    ImGui::GetColorU32(ImGuiCol_NavCursor), 2.f * scale);
            const ThemeColor color = !buttonEnabled ? ThemeColor::TextDisabled : index == 0
                ? (running ? ThemeColor::Error : ThemeColor::Positive)
                : paused ? ThemeColor::Primary : ThemeColor::Text;
            unsigned int codepoint{};
            ImTextCharFromUtf8(&codepoint, icon, nullptr);
            const auto* glyph = ImGui::GetFontBaked()->FindGlyph(static_cast<ImWchar>(codepoint));
            const ImVec2 textPos{ p.x + width * .5f - (glyph->X0 + glyph->X1) * .5f,
                p.y + (height - 2.f * scale) * .5f - (glyph->Y0 + glyph->Y1) * .5f };
            draw->AddText(textPos, ImGui::GetColorU32(editor::ThemeColorValue(color)), icon);
            if (active)
                draw->AddRectFilled({ p.x + 5.f * scale, p.y + height - 3.f * scale },
                    { p.x + width - 5.f * scale, p.y + height - 2.f * scale },
                    ImGui::GetColorU32(editor::ThemeColorValue(ThemeColor::Primary)));
            if (hovered) ImGui::SetTooltip("%s", tip);
            ImGui::EndDisabled();
            if (!pressed) continue;
            preview.editorPreviewActive = true;
            if (index == 0)
            {
                preview.editorPreviewPlaying = !running;
                preview.editorPreviewPaused = false;
                if (running) preview.timeElapsed = 0.f;
            }
            else
            {
                preview.editorPreviewPlaying = paused;
                preview.editorPreviewPaused = !paused;
            }
        }
        ImGui::EndGroup();
        ImGui::PopID();
        ImGui::PopFont();
    }

    bool DrawBonePicker(const Animator& animator, const char* label,
        std::string& selected)
    {
        bool changed = false;
        if (ImGui::BeginCombo(label, selected.empty() ? "Select bone" : selected.c_str()))
        {
            for (std::size_t index = 0; index < animator.GetBoneCount(); ++index)
            {
                const std::string name = animator.GetBoneName(static_cast<int>(index));
                if (ImGui::Selectable(name.c_str(), name == selected))
                {
                    selected = name;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    bool DrawConstraintFields(const Animator& animator, TwoBoneIKConstraint& value)
    {
        bool changed = false;
        changed |= DrawBonePicker(animator, "Start Bone", value.StartBone);
        changed |= DrawBonePicker(animator, "Middle Bone", value.MiddleBone);
        changed |= DrawBonePicker(animator, "End Bone", value.EndBone);
        changed |= ImGui::DragFloat3("Target (World)", &value.TargetWorld.x, .05f);
        changed |= ImGui::DragFloat3("Pole (World)", &value.PoleWorld.x, .05f);
        changed |= ImGui::SliderFloat("Weight", &value.Weight, 0.f, 1.f);
        changed |= ImGui::Checkbox("Enabled", &value.Enabled);
        changed |= ImGui::Checkbox("Required", &value.Required);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Required IK keeps this Animator at full quality in game playback.");
        return changed;
    }

    bool DrawConstraintFields(const Animator& animator, BoneTransformConstraint& value)
    {
        bool changed = DrawBonePicker(animator, "Bone", value.Bone);
        changed |= ImGui::DragFloat3("Translation Offset", &value.TranslationOffset.x, .05f);
        changed |= ImGui::DragFloat4("Rotation Offset (Quaternion)",
            &value.RotationOffset.x, .01f);
        changed |= ImGui::DragFloat3("Scale Multiplier", &value.ScaleMultiplier.x, .01f);
        changed |= ImGui::SliderFloat("Weight", &value.Weight, 0.f, 1.f);
        changed |= ImGui::Checkbox("Enabled", &value.Enabled);
        changed |= ImGui::Checkbox("Required", &value.Required);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Required correction keeps this Animator at full quality in game playback.");
        return changed;
    }

    template<class Constraint>
    void DrawConstraintList(Animator& animator, std::vector<Constraint>& constraints,
        const char* title, const char* field)
    {
        ImGui::PushID(field);
        if (ImGui::CollapsingHeader(title))
        {
            if (ImGui::Button("Add constraint"))
            {
                auto before = Meta::SerializeDocument(&animator);
                constraints.emplace_back();
                EditorObjectOperations::CommitProperty(animator, field, std::move(before));
            }
            for (std::size_t index = 0; index < constraints.size(); ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                const std::string label = "Constraint " + std::to_string(index + 1);
                const bool open = ImGui::TreeNodeEx("##Constraint",
                    ImGuiTreeNodeFlags_None, "%s", label.c_str());
                ImGui::SameLine();
                const bool remove = ImGui::SmallButton("Remove");
                if (open)
                {
                    Constraint edited = constraints[index];
                    if (!remove && DrawConstraintFields(animator, edited))
                    {
                        auto before = Meta::SerializeDocument(&animator);
                        constraints[index] = std::move(edited);
                        EditorObjectOperations::CommitProperty(animator,
                            field, std::move(before));
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
                if (remove)
                {
                    auto before = Meta::SerializeDocument(&animator);
                    constraints.erase(constraints.begin() + index);
                    EditorObjectOperations::CommitProperty(animator, field, std::move(before));
                    break;
                }
            }
        }
        ImGui::PopID();
    }
}

void ImGuiDrawHelperAnimator(Animator* animator)
{
	if (!animator) return;

	Meta::TypedDraw::DrawOwnMembers(*animator);

	const bool editMode = !SceneManagers->IsGameStart()
		&& !SceneManagers->IsPlayCommitted();
	auto& preview = animator->GetInstance();
	const int clipCount = static_cast<int>(animator->GetClipCount());
	const int selectedClip = static_cast<int>(animator->GetSelectedClipIndex());
	const bool validClip = selectedClip >= 0 && selectedClip < clipCount;
	const file::path modelPath = DataSystems->GetFilePath(animator->m_Motion);
	if (animator->TypedSkeleton())
		ImGui::Text("Model: %s", modelPath.empty() ? "Bound model"
			: modelPath.filename().string().c_str());
	else if (animator->m_Motion != FileGuid{})
		ImGui::TextDisabled("Model: animation data could not be loaded");
	else
		ImGui::TextDisabled("Model: none assigned");

	ImGui::TextUnformatted("Preview Clip");
	ImGui::SetNextItemWidth(ImMax(120.f,
		(ImGui::GetContentRegionAvail().x - 78.f) * .5f));
	ImGui::BeginDisabled(!editMode || clipCount == 0);
	const std::string selectedName = validClip
		? animator->GetClipName(selectedClip) : std::string{ "Select a clip" };
	if (ImGui::BeginCombo("##PreviewClip", selectedName.c_str()))
	{
		for (int index = 0; index < clipCount; ++index)
		{
			const std::string clipName = animator->GetClipName(index);
			ImGui::PushID(index);
			if (ImGui::Selectable(clipName.c_str(), index == selectedClip))
			{
				auto before = Meta::SerializeDocument(animator);
				animator->SetSelectedClipIndex(static_cast<uint32_t>(index));
				preview.editorPreviewActive = true;
				preview.timeElapsed = 0.f;
				EditorObjectOperations::CommitProperty(*animator,
					"m_AnimIndexChosen", std::move(before));
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	DrawPreviewPlaybackControls(preview, editMode && clipCount > 0 && validClip);
	if (!editMode) ImGui::TextDisabled("Clip preview is available in Edit mode only.");
	if (!editMode)
		ImGui::TextDisabled("Animation quality: L%d",
			static_cast<int>(preview.qualityStage));
	if (!animator->IsDirectEditorPreview())
	{
		for (const auto& controller : animator->m_animationControllers)
		{
			if (!controller || !controller->useController
				|| controller->m_owner != animator
				|| !controller->GetCurrentState()) continue;
			ImGui::TextDisabled("Current state (%s): %s", controller->name.c_str(),
				controller->GetCurrentState()->m_name.c_str());
			break;
		}
	}

	const editor::widgets::property_sheet sheet(g_animatorLayout, { "Clip", "Loop", "Key Frame Event", "Controllers" });
	if (ImGui::CollapsingHeader("animations"))
	{
		// I5-D5b — 열거·이름도 창구를 지난다. D4e-2가 편집을 Animator
		// 소유로 옮겼으나 목록은 공유 자산을 직접 훑고 있었다 — 인덱스
		// 축이 두 출처로 갈리면 편집 정본과 표시 대상이 어긋난다.
		const int clipCount = static_cast<int>(animator->GetClipCount());
		for (int i = 0; i < clipCount; ++i)
		{
			std::string clipName = animator->GetClipName(i);
			// 이름이 같은 클립이 둘일 수 있다 — ID 는 인덱스로 묶는다.
			ImGui::PushID(i);
			ImGui::SetNextItemWidth(sheet.line("Clip"));
			ImGui::InputText("##ClipName", &clipName, ImGuiInputTextFlags_ReadOnly);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", clipName.c_str());

			// I5-D4e-2 — 루프·이벤트 편집의 정본은 Animator 클립
			// 오버라이드다. 구 코드는 공유 자산(m_Skeleton->m_animations)을
			// 직접 편집해 같은 스켈레톤을 공유하는 다른 Animator까지
			// 바뀌었다 — 발화·저장 정본과 편집 표면을 함께 옮긴다.
			bool looping = animator->IsClipLooping(i);
			sheet.line("Loop");
			if (ImGui::Checkbox("##Loop", &looping))
			{
				animator->SetClipLooping(i, looping);
			}
			const bool openEvents = ImGui::Button("Edit###KeyFrameEvent", ImVec2(sheet.line("Key Frame Event"), 0.f));
			ImGui::PopID();
			if (openEvents)
			{
				::editor::animator_editing::select_clip(i);
				ToggleAnimatorWindow(EditorWindowName::kAnimatorEvent);
			}
			ImGui::Separator();
		}
	}

	if (ImGui::Button("Edit###AnimationControllers", ImVec2(sheet.line("Controllers"), 0.f)))
	{
		ToggleAnimatorWindow(EditorWindowName::kAnimationControllers);
	}

	if (ImGui::CollapsingHeader("Animation Quality"))
	{
		ImGui::TextDisabled("Game playback settings; editor preview uses full quality.");
		float radius = animator->m_QualityRadius;
		if (ImGui::DragFloat("Visibility Radius", &radius, .05f, .01f, 10000.f)
			&& std::isfinite(radius))
		{
			auto before = Meta::SerializeDocument(animator);
			animator->m_QualityRadius = (std::max)(radius, .01f);
			EditorObjectOperations::CommitProperty(*animator,
				"m_QualityRadius", std::move(before));
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Approximate character size used for visibility and screen size.");
		int lowBones = static_cast<int>((std::min)(animator->m_LowDetailBoneCount,
			static_cast<std::uint32_t>(INT_MAX)));
		if (ImGui::InputInt("Reduced Bone Count", &lowBones))
		{
			const auto boneCount = animator->GetBoneCount();
			const int maximum = boneCount > 1
				? static_cast<int>((std::min)(boneCount - 1,
					static_cast<std::size_t>(INT_MAX))) : 0;
			auto before = Meta::SerializeDocument(animator);
			animator->m_LowDetailBoneCount = static_cast<std::uint32_t>(
				(std::clamp)(lowBones, 0, maximum));
			EditorObjectOperations::CommitProperty(*animator,
				"m_LowDetailBoneCount", std::move(before));
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("0 disables reduced bones. Use a parent-first prefix smaller than the full skeleton.");
		bool fullQuality = animator->m_ForceFullQuality;
		if (ImGui::Checkbox("Always Full Quality", &fullQuality))
		{
			auto before = Meta::SerializeDocument(animator);
			animator->m_ForceFullQuality = fullQuality;
			EditorObjectOperations::CommitProperty(*animator,
				"m_ForceFullQuality", std::move(before));
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Keeps this Animator at full quality even when the animation CPU budget is tight.");
	}
	DrawConstraintList(*animator, animator->m_TwoBoneIKConstraints,
		"Two-Bone IK", "m_TwoBoneIKConstraints");
	DrawConstraintList(*animator, animator->m_BoneTransformConstraints,
		"Bone Transform Corrections", "m_BoneTransformConstraints");
}
