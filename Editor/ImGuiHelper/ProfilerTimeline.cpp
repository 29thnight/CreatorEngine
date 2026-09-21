// PHASE 14 P3 — Timeline.
//
// §7.3 의 CPU 트랙이다. 스레드마다 레인 하나, 구간마다 사각형 하나, 깊이는
// 아래로 쌓는다.
//
// ★ 이 파일에는 정렬도 집계도 없다. 그리는 스팬은 코어가 이미 전위 순서로
//   세워 둔 것(frame_aggregate::spans())이고 레인 경계도 코어가 적어 둔
//   것(thread_summary::span_begin/end)이다. 여기서 다시 정렬하면 두 정렬이
//   갈리는 순간 표와 타임라인이 서로 다른 트리를 말하게 된다.
//
// ★ 가로 시야(확대·이동)도 reader 가 든다. 구간 밖으로 나가지 않는다는 계약을
//   코어 프로브가 물고 있어서, 여기서는 그 결과를 픽셀로 옮기기만 한다.
#include "ProfilerView.h"

#include <algorithm>
#include <cstdio>

#include "ImGui.h"
#include "ProfileMarker.h"
#include "ProfileScope.h"

namespace editor::profiler_view
{
	namespace
	{
		constexpr float kLaneHeaderWidth = 150.0f;
		constexpr float kRowHeight = 18.0f;
		constexpr float kLanePadding = 6.0f;
		constexpr float kMinimumSpanWidth = 1.0f;
		constexpr float kTimelineHeight = 260.0f;

		// 깊이마다 색을 달리해 중첩이 눈에 들어오게 한다. 마커 id 를 섞어
		// 같은 깊이의 이웃이 붙어 보이지 않게 한다.
		ImU32 span_color(ce::marker_id marker, std::uint16_t depth, bool truncated)
		{
			if (truncated)
			{
				// 잘린 구간은 길이가 실제보다 짧다. 색으로 구분해 두지 않으면
				// 옆 구간과 나란히 읽힌다.
				return IM_COL32(150, 110, 90, 255);
			}

			const std::uint32_t hash = (static_cast<std::uint32_t>(marker) * 2654435761u) >> 16;
			const int base = 90 + static_cast<int>(hash % 60);
			const int lift = 18 * (static_cast<int>(depth) % 4);
			return IM_COL32((std::min)(base + lift + 40, 235),
			                (std::min)(base + lift, 210),
			                (std::min)(base + 60, 230), 255);
		}
	}

	void draw_timeline()
	{
		// 타임라인이 **그려졌다** 는 증거. 탭은 선택돼야 본문이 돌므로,
		// 창이 열린 것만으로는 여기까지 온다고 말할 수 없다.
		ce::profile_scope _profile{ ce::marker<"ProfilerTimeline">() };

		ce::capture_reader& view = reader();
		if (!view.has_capture())
		{
			ImGui::TextDisabled("얼린 캡처가 없다 - Pause 를 누를 것");
			return;
		}

		const ce::frame_aggregate& aggregate = view.aggregate();
		const std::span<const ce::profile_event> spans = aggregate.spans();
		if (spans.empty())
		{
			ImGui::TextDisabled("선택한 구간에 이벤트가 없다");
			return;
		}

		const ce::profile_tick viewBegin = view.view_begin();
		const ce::profile_tick viewSpan = view.view_span();
		if (viewSpan == 0)
		{
			return;
		}

		// 배율을 글로도 낸다. 그림만 보면 지금 몇 ms 를 보고 있는지 모른다.
		ImGui::Text("시야 %.3f ms  ·  전체 %.3f ms",
		            ticks_to_milliseconds(viewSpan),
		            ticks_to_milliseconds(aggregate.tick_end() - aggregate.tick_begin()));
		ImGui::SameLine();
		if (ImGui::SmallButton("전체 보기"))
		{
			view.reset_view();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(휠: 확대 · 끌기: 이동)");

		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float width = (std::max)(ImGui::GetContentRegionAvail().x, 200.0f);
		const ImVec2 size(width, kTimelineHeight);

		ImGui::InvisibleButton("##ProfilerTimeline", size,
		                       ImGuiButtonFlags_MouseButtonLeft);
		const bool hovered = ImGui::IsItemHovered();
		const bool active = ImGui::IsItemActive();

		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
		draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
		                    IM_COL32(20, 22, 26, 255));

		const float plotLeft = origin.x + kLaneHeaderWidth;
		const float plotWidth = (std::max)(size.x - kLaneHeaderWidth, 32.0f);
		const double ticksPerPixel = static_cast<double>(viewSpan) / plotWidth;

		auto tick_to_x = [&](ce::profile_tick tick) -> float
		{
			const double offset = static_cast<double>(tick) - static_cast<double>(viewBegin);
			return plotLeft + static_cast<float>(offset / ticksPerPixel);
		};

		// 레인. 스레드 순서는 코어가 정한 것(slot 오름차순)을 그대로 쓴다 —
		// §7.3 의 트랙 순서는 GPU 트랙이 서는 P4 에서 함께 정한다.
		float laneTop = origin.y + kLanePadding;
		const ce::profile_event* hoveredSpan = nullptr;

		for (const ce::thread_summary& thread : aggregate.threads())
		{
			const float laneHeight =
				static_cast<float>(thread.max_depth + 1) * kRowHeight + kLanePadding;
			if (laneTop > origin.y + size.y)
			{
				break;
			}

			draw->AddText(ImVec2(origin.x + 4.0f, laneTop),
			              IM_COL32(200, 205, 215, 255),
			              thread_name(view.capture(), thread.thread_slot));

			char lane[64];
			std::snprintf(lane, sizeof(lane), "%.3f ms",
			              ticks_to_milliseconds(thread.root_ticks));
			draw->AddText(ImVec2(origin.x + 4.0f, laneTop + kRowHeight * 0.85f),
			              IM_COL32(130, 140, 155, 255), lane);

			for (std::uint32_t i = thread.span_begin;
			     i < thread.span_end && i < spans.size(); ++i)
			{
				const ce::profile_event& span = spans[i];
				if (span.tick_end < viewBegin || span.tick_begin > viewBegin + viewSpan)
				{
					continue;   // 시야 밖
				}

				const float x0 = tick_to_x(span.tick_begin);
				const float x1 = (std::max)(tick_to_x(span.tick_end), x0 + kMinimumSpanWidth);
				const float y0 = laneTop + static_cast<float>(span.depth) * kRowHeight;
				const float y1 = y0 + kRowHeight - 2.0f;

				const bool truncated =
					ce::has_flag(span.flags, ce::event_flags::truncated_begin) ||
					ce::has_flag(span.flags, ce::event_flags::truncated_end);

				draw->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1),
				                    span_color(span.marker, span.depth, truncated));

				// 이름은 칸이 넉넉할 때만. 잘라 그린 글자도 항목은 전체 폭을
				// 차지하므로 DrawList 로 직접 그리고 클립으로 막는다.
				if (x1 - x0 > 28.0f)
				{
					draw->PushClipRect(ImVec2(x0 + 2.0f, y0), ImVec2(x1 - 1.0f, y1), true);
					draw->AddText(ImVec2(x0 + 3.0f, y0 + 1.0f), IM_COL32(20, 22, 26, 255),
					              ce::marker_info(span.marker).name);
					draw->PopClipRect();
				}

				if (hovered)
				{
					const ImVec2 mouse = ImGui::GetIO().MousePos;
					if (mouse.x >= x0 && mouse.x <= x1 && mouse.y >= y0 && mouse.y <= y1)
					{
						hoveredSpan = &span;
					}
				}
			}

			laneTop += laneHeight;
			draw->AddLine(ImVec2(origin.x, laneTop - kLanePadding * 0.5f),
			              ImVec2(origin.x + size.x, laneTop - kLanePadding * 0.5f),
			              IM_COL32(60, 64, 72, 255));
		}

		draw->AddLine(ImVec2(plotLeft, origin.y), ImVec2(plotLeft, origin.y + size.y),
		              IM_COL32(80, 86, 96, 255));
		draw->PopClipRect();

		if (hoveredSpan)
		{
			const ce::profile_tick length = (hoveredSpan->tick_end > hoveredSpan->tick_begin)
				? (hoveredSpan->tick_end - hoveredSpan->tick_begin) : 0;
			ImGui::SetTooltip("%s\n%.4f ms  ·  depth %u  ·  frame %u%s",
			                  ce::marker_info(hoveredSpan->marker).name,
			                  ticks_to_milliseconds(length),
			                  static_cast<unsigned>(hoveredSpan->depth),
			                  hoveredSpan->frame,
			                  (ce::has_flag(hoveredSpan->flags, ce::event_flags::truncated_begin) ||
			                   ce::has_flag(hoveredSpan->flags, ce::event_flags::truncated_end))
				                  ? "\n(잘린 구간 - 길이가 실제보다 짧다)" : "");
		}

		// 휠로 확대. 커서 아래의 tick 을 제자리에 둔다 — 그러지 않으면 확대할
		// 때마다 보던 것이 화면 밖으로 밀려난다.
		if (hovered)
		{
			const float wheel = ImGui::GetIO().MouseWheel;
			if (wheel != 0.0f)
			{
				const double offset =
					static_cast<double>(ImGui::GetIO().MousePos.x - plotLeft) * ticksPerPixel;
				const ce::profile_tick pivot =
					viewBegin + static_cast<ce::profile_tick>((std::max)(offset, 0.0));
				view.zoom_view(wheel > 0.0f ? 0.8 : 1.25, pivot);
			}
		}

		// 끌어서 이동. 화면에서 왼쪽으로 끌면 뒤쪽을 본다.
		if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			const float dragX = ImGui::GetIO().MouseDelta.x;
			if (dragX != 0.0f)
			{
				view.pan_view(static_cast<std::int64_t>(-dragX * ticksPerPixel));
			}
		}
	}
}
