// PHASE 14 P3 — Frame Overview.
//
// §7.2 가 "모든 분석의 entry point" 라고 부른 그래프다. 프레임마다 막대 하나,
// 클릭으로 한 프레임, 끌어서 범위를 고른다.
//
// ★ 여기서 재는 것은 frame_record 의 tick_begin..tick_end 다. 스레드별 합계가
//   아니다 — 워커 구간은 서로 겹치고 소유자의 대기와도 겹치므로 더하면 프레임
//   길이보다 커진다(AnimationJob 주석이 같은 것을 못 박았다).
#include "ProfilerView.h"

#include <algorithm>
#include <cstdio>

#include "ImGui.h"
#include "ProfileService.h"

namespace editor::profiler_view
{
	namespace
	{
		// 예산선. 60/30 FPS.
		constexpr double kBudget60 = 1000.0 / 60.0;
		constexpr double kBudget30 = 1000.0 / 30.0;

		constexpr float kGraphHeight = 96.0f;
		constexpr float kMinimumBarWidth = 1.0f;

		ImU32 bar_color(double milliseconds, bool selected)
		{
			if (selected)
			{
				return IM_COL32(120, 200, 255, 255);
			}
			if (milliseconds > kBudget30)
			{
				return IM_COL32(220, 90, 80, 255);
			}
			if (milliseconds > kBudget60)
			{
				return IM_COL32(220, 180, 80, 255);
			}
			return IM_COL32(110, 130, 150, 255);
		}
	}

	void draw_frame_overview()
	{
		ce::capture_reader& view = reader();
		const ce::capture_session* capture = view.capture();
		if (!capture || capture->frame_count() == 0)
		{
			ImGui::TextDisabled("얼린 캡처가 없다 - Pause 를 누르면 그때까지의 프레임이 선다");
			return;
		}

		const std::span<const ce::frame_record> frames = capture->frames();

		// 가장 긴 프레임으로 정규화한다. 예산선이 늘 보이도록 하한을 둔다 —
		// 모든 프레임이 1 ms 일 때 16.67 선이 그래프 밖으로 나가면 예산이
		// 있는지조차 알 수 없다.
		double peak = kBudget60 * 1.25;
		for (const ce::frame_record& frame : frames)
		{
			peak = (std::max)(peak, ticks_to_milliseconds(frame.tick_end - frame.tick_begin));
		}

		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float width = (std::max)(ImGui::GetContentRegionAvail().x, 64.0f);
		const ImVec2 size(width, kGraphHeight);

		ImGui::InvisibleButton("##ProfilerFrameOverview", size);
		const bool hovered = ImGui::IsItemHovered();
		const bool active = ImGui::IsItemActive();

		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
		                    IM_COL32(24, 26, 30, 255));

		const std::size_t count = frames.size();
		const float barWidth = (std::max)(size.x / static_cast<float>(count), kMinimumBarWidth);

		// 막대. 선택 구간은 색으로 구분한다.
		for (std::size_t i = 0; i < count; ++i)
		{
			const ce::frame_record& frame = frames[i];
			const double milliseconds =
				ticks_to_milliseconds(frame.tick_end - frame.tick_begin);
			const float normalized = static_cast<float>(
				(std::min)(milliseconds / peak, 1.0));
			const float height = normalized * size.y;

			const float x0 = origin.x + static_cast<float>(i) * size.x / static_cast<float>(count);
			const float x1 = x0 + barWidth;
			const bool selected = frame.engine_frame >= view.selected_first()
				&& frame.engine_frame <= view.selected_last();

			draw->AddRectFilled(ImVec2(x0, origin.y + size.y - height),
			                    ImVec2(x1, origin.y + size.y),
			                    bar_color(milliseconds, selected));
		}

		// 예산선.
		for (const double budget : { kBudget60, kBudget30 })
		{
			if (budget > peak)
			{
				continue;
			}
			const float y = origin.y + size.y -
				static_cast<float>(budget / peak) * size.y;
			draw->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + size.x, y),
			              IM_COL32(200, 200, 200, 90));
			char label[32];
			std::snprintf(label, sizeof(label), "%.2f ms", budget);
			draw->AddText(ImVec2(origin.x + 4.0f, y - ImGui::GetTextLineHeight()),
			              IM_COL32(200, 200, 200, 140), label);
		}

		// 마우스 → 프레임.
		auto frame_at = [&](float mouseX) -> std::size_t
		{
			const float local = (std::clamp)(mouseX - origin.x, 0.0f, size.x - 0.5f);
			const std::size_t index = static_cast<std::size_t>(
				local / size.x * static_cast<float>(count));
			return (std::min)(index, count - 1);
		};

		if (hovered || active)
		{
			const std::size_t index = frame_at(ImGui::GetIO().MousePos.x);
			const ce::frame_record& frame = frames[index];
			ImGui::SetTooltip("frame %u\n%.3f ms\n이벤트 %zu",
			                  frame.engine_frame,
			                  ticks_to_milliseconds(frame.tick_end - frame.tick_begin),
			                  frame.events.size());
		}

		// ★ 고르는 순간 따라가기를 끈다. 그러지 않으면 다음 캡처가 올 때
		//   선택이 최신으로 튀어, 붙잡아 둔 프레임이 손에서 빠져나간다.
		if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			const std::size_t from = frame_at(ImGui::GetIO().MouseClickedPos[0].x);
			const std::size_t to = frame_at(ImGui::GetIO().MousePos.x);
			view.set_live_follow(false);
			view.select_range(frames[from].engine_frame, frames[to].engine_frame);
		}
		else if (ImGui::IsItemDeactivated() && !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left))
		{
			view.set_live_follow(false);
			view.select_frame(frames[frame_at(ImGui::GetIO().MousePos.x)].engine_frame);
		}

		// 보존 구간과 선택을 글로도 낸다. 그래프만으로는 어느 프레임인지
		// 읽을 수 없다.
		if (view.selected_count() > 1)
		{
			ImGui::Text("frames %u..%u (%u개)  ·  보존 %u..%u",
			            view.selected_first(), view.selected_last(), view.selected_count(),
			            view.available_first(), view.available_last());
		}
		else
		{
			ImGui::Text("frame %u  ·  보존 %u..%u",
			            view.selected_first(),
			            view.available_first(), view.available_last());
		}
	}
}
