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
#include <cmath>
#include <cstdio>

#include "ImGui.h"
#include "ProfileMarker.h"
#include "ProfileScope.h"

namespace editor::profiler_view
{
	namespace
	{
		constexpr float kLaneHeaderWidth = 150.0f;
		constexpr float kLanePadding = 6.0f;
		constexpr float kMinimumSpanWidth = 1.0f;

		// ── 줄 높이는 폰트가 정한다 ────────────────────────────────────────
		//
		// ★ 여기에 18 px 이 박혀 있었다. 에디터가 한글 폰트를 얹으면서 한 줄이
		//   그보다 커졌고, 그때부터 레인 이름과 그 아래 ms 가 **서로 겹쳐**
		//   그려졌다. 막대 안의 마커 이름도 위아래가 잘렸다.
		//
		//   수치로는 한 군데도 안 어긋난다 — 접는 일은 전부 코어가 하고 이
		//   층은 그리기만 하므로, 프로브가 무는 숫자는 전부 그대로다. 눈으로만
		//   잡히는 종류의 결함이라 화면을 한 번 떠 보기 전까지 몰랐다.
		constexpr float kRowTextPadding = 4.0f;

		// §7.3 의 첫째 트랙 — 프레임 경계와 길이 없는 사건이 사는 띠. 제 글자
		// 한 줄이 들어갈 만큼만 높다.
		constexpr float kStripTextPadding = 6.0f;

		// 한 번에 보여 주는 줄 수. 전체 높이를 픽셀로 박으면 폰트가 커졌을 때
		// 맨 아래 레인(트랙 순서상 GPU)이 화면 밖으로 밀린다.
		constexpr int kVisibleRows = 14;

		// 레인 머리글은 이름과 ms 두 줄이다. 깊이가 0 인 레인이라도 두 줄은
		// 확보해야 아래 레인의 이름 위에 ms 가 얹히지 않는다.
		constexpr int kLaneHeaderRows = 2;

		constexpr float kInstantMarkRadius = 4.0f;

		// 깊이마다 색을 달리해 중첩이 눈에 들어오게 한다. 마커 id 를 섞어
		// 같은 깊이의 이웃이 붙어 보이지 않게 한다.
		ImU32 span_color(ce::marker_id marker, std::uint16_t depth, bool truncated,
		                 bool gpu)
		{
			if (gpu)
			{
				// GPU 구간은 CPU 스코프와 **시각의 뜻이 다르다** — 늦게 도착해
				// 제 프레임 칸으로 되돌려진 것이고, 깊이도 언제나 0 이다.
				// 같은 색으로 그리면 나란히 선 CPU 구간과 한 트리처럼 읽힌다.
				const std::uint32_t hash = (static_cast<std::uint32_t>(marker) * 2654435761u) >> 16;
				const int lift = static_cast<int>(hash % 50);
				return IM_COL32(70 + lift, 150 + lift, 200, 255);
			}

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

		const float rowHeight = ImGui::GetTextLineHeight() + kRowTextPadding;
		const float stripHeight = ImGui::GetTextLineHeight() + kStripTextPadding;

		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float width = (std::max)(ImGui::GetContentRegionAvail().x, 200.0f);
		const ImVec2 size(width, stripHeight + static_cast<float>(kVisibleRows) * rowHeight);

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

		// ── §7.3 트랙 1: 프레임 경계와 사건 ────────────────────────────────
		//
		// ★ 맨 위에 둔다. 아래 레인의 막대가 어느 프레임의 것인지는 이 띠가
		//   없으면 읽을 수 없다 — 확대하면 프레임 번호가 화면에서 사라지고,
		//   그때 타임라인은 "무언가 오래 걸린다" 까지만 말한다.
		const float stripTop = origin.y;
		const float stripBottom = stripTop + stripHeight;
		const ce::frame_boundary* hoveredFrame = nullptr;
		const ce::profile_event* hoveredInstant = nullptr;

		draw->AddRectFilled(ImVec2(origin.x, stripTop), ImVec2(origin.x + size.x, stripBottom),
		                    IM_COL32(28, 31, 37, 255));
		draw->AddText(ImVec2(origin.x + 4.0f, stripTop + 3.0f),
		              IM_COL32(150, 158, 172, 255), "Frames");

		for (const ce::frame_boundary& boundary : aggregate.boundaries())
		{
			if (boundary.tick_end < viewBegin || boundary.tick_begin > viewBegin + viewSpan)
			{
				continue;
			}

			const float x0 = (std::max)(tick_to_x(boundary.tick_begin), plotLeft);
			const float x1 = tick_to_x(boundary.tick_end);

			// 경계선은 띠만이 아니라 **레인 전체를 가른다.** 띠 안에만 그으면
			// 아래 막대와 눈으로 맞춰야 하고, 그 맞춤은 확대할수록 틀어진다.
			draw->AddLine(ImVec2(x0, stripTop), ImVec2(x0, origin.y + size.y),
			              IM_COL32(70, 78, 92, 255));

			// 번호는 칸이 넉넉할 때만. 좁으면 선만 남는다.
			if (x1 - x0 > 34.0f)
			{
				char label[32];
				std::snprintf(label, sizeof(label), "%u", boundary.engine_frame);
				draw->PushClipRect(ImVec2(x0 + 2.0f, stripTop), ImVec2(x1 - 1.0f, stripBottom), true);
				draw->AddText(ImVec2(x0 + 3.0f, stripTop + 3.0f),
				              IM_COL32(190, 198, 212, 255), label);
				draw->PopClipRect();
			}

			if (hovered)
			{
				const ImVec2 mouse = ImGui::GetIO().MousePos;
				if (mouse.x >= x0 && mouse.x <= x1 &&
				    mouse.y >= stripTop && mouse.y <= stripBottom)
				{
					hoveredFrame = &boundary;
				}
			}
		}

		// 길이가 없는 사건. 레인이 아니라 여기 모인다 — 어느 스레드가 냈든
		// "언제 일어났는가" 가 이 트랙이 답하는 물음이기 때문이다.
		for (const ce::profile_event& instant : aggregate.instants())
		{
			if (instant.tick_begin < viewBegin || instant.tick_begin > viewBegin + viewSpan)
			{
				continue;
			}

			const float x = tick_to_x(instant.tick_begin);
			if (x < plotLeft) continue;

			const float y = stripBottom - kInstantMarkRadius - 1.0f;
			const ImVec2 points[3] = {
				ImVec2(x, y - kInstantMarkRadius),
				ImVec2(x - kInstantMarkRadius, y + kInstantMarkRadius),
				ImVec2(x + kInstantMarkRadius, y + kInstantMarkRadius),
			};
			draw->AddTriangleFilled(points[0], points[1], points[2],
			                        IM_COL32(240, 190, 90, 255));
			draw->AddLine(ImVec2(x, stripBottom), ImVec2(x, origin.y + size.y),
			              IM_COL32(150, 120, 60, 160));

			if (hovered)
			{
				const ImVec2 mouse = ImGui::GetIO().MousePos;
				if (std::abs(mouse.x - x) <= kInstantMarkRadius + 2.0f &&
				    mouse.y >= stripTop && mouse.y <= stripBottom)
				{
					hoveredInstant = &instant;
				}
			}
		}

		// 레인. 순서는 코어가 정한 §7.3 의 트랙 순서(game → command/worker →
		// script → GPU)를 그대로 쓴다. 여기서 다시 세우면 그 순서가 옳은지
		// 물을 수단이 눈뿐이 된다 — 지금은 코어 프로브가 묻는다.
		float laneTop = stripBottom + kLanePadding;
		const ce::profile_event* hoveredSpan = nullptr;

		for (const ce::thread_summary& thread : aggregate.threads())
		{
			const int laneRows =
				(std::max)(static_cast<int>(thread.max_depth) + 1, kLaneHeaderRows);
			const float laneHeight = static_cast<float>(laneRows) * rowHeight + kLanePadding;
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
			draw->AddText(ImVec2(origin.x + 4.0f, laneTop + rowHeight),
			              IM_COL32(130, 140, 155, 255), lane);

			for (std::uint32_t i = thread.span_begin;
			     i < thread.span_end && i < spans.size(); ++i)
			{
				const ce::profile_event& span = spans[i];

				// 길이가 없는 사건은 위의 경계 띠가 그린다. 여기서도 그리면
				// 폭 0 짜리 막대가 레인마다 겹쳐 서고, 같은 것이 두 자리에서
				// 서로 다른 뜻으로 읽힌다.
				if (ce::has_flag(span.flags, ce::event_flags::instant))
				{
					continue;
				}

				if (span.tick_end < viewBegin || span.tick_begin > viewBegin + viewSpan)
				{
					continue;   // 시야 밖
				}

				const float x0 = tick_to_x(span.tick_begin);
				const float x1 = (std::max)(tick_to_x(span.tick_end), x0 + kMinimumSpanWidth);
				const float y0 = laneTop + static_cast<float>(span.depth) * rowHeight;
				const float y1 = y0 + rowHeight - 2.0f;

				const bool truncated =
					ce::has_flag(span.flags, ce::event_flags::truncated_begin) ||
					ce::has_flag(span.flags, ce::event_flags::truncated_end);
				const bool gpu = ce::has_flag(span.flags, ce::event_flags::gpu_span);

				draw->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1),
				                    span_color(span.marker, span.depth, truncated, gpu));

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

		if (hoveredInstant)
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(ce::marker_info(hoveredInstant->marker).name);
			ImGui::Text("frame %u  ·  %s", hoveredInstant->frame,
			            thread_name(view.capture(), hoveredInstant->thread_slot));
			ImGui::TextDisabled("길이가 없는 사건 - 일어난 순간만 있다");
			ImGui::EndTooltip();
		}
		else if (hoveredFrame)
		{
			const ce::profile_tick length = (hoveredFrame->tick_end > hoveredFrame->tick_begin)
				? (hoveredFrame->tick_end - hoveredFrame->tick_begin) : 0;
			ImGui::SetTooltip("frame %u\n%.4f ms", hoveredFrame->engine_frame,
			                  ticks_to_milliseconds(length));
		}
		else if (hoveredSpan)
		{
			const ce::profile_tick length = (hoveredSpan->tick_end > hoveredSpan->tick_begin)
				? (hoveredSpan->tick_end - hoveredSpan->tick_begin) : 0;
			const bool truncated =
				ce::has_flag(hoveredSpan->flags, ce::event_flags::truncated_begin) ||
				ce::has_flag(hoveredSpan->flags, ce::event_flags::truncated_end);

			ImGui::BeginTooltip();
			ImGui::TextUnformatted(ce::marker_info(hoveredSpan->marker).name);
			ImGui::Text("%.4f ms  ·  frame %u", ticks_to_milliseconds(length),
			            hoveredSpan->frame);

			if (ce::has_flag(hoveredSpan->flags, ce::event_flags::gpu_span))
			{
				// §7.3 이 GPU bar 에 싣기로 한 것들. 패스 이름은 위의 마커다.
				//
				// ★ 제출 번호와 뷰가 있어야 쓸모가 있다. 같은 프레임에 씬뷰와
				//   게임뷰의 제출이 나란히 서므로, 이름만으로는 같은 패스가
				//   두 번 그려진 것처럼 보인다 — §0.5.10 이 83% 를 못 보던
				//   이유가 정확히 그 구분의 부재였다.
				ImGui::Separator();
				ImGui::Text("GPU  ·  submission %u  ·  view %u  ·  queue %u",
				            hoveredSpan->submission,
				            static_cast<unsigned>(hoveredSpan->view),
				            static_cast<unsigned>(hoveredSpan->queue));
				ImGui::TextDisabled("펜스가 끝난 뒤에야 읽힌다 - 제 프레임 칸으로 돌려보낸 것이다");
			}
			else
			{
				ImGui::Text("depth %u", static_cast<unsigned>(hoveredSpan->depth));
			}

			if (truncated)
			{
				ImGui::TextDisabled("잘린 구간 - 길이가 실제보다 짧다");
			}
			ImGui::EndTooltip();
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
