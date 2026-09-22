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
#include <iterator>

#include "ImGui.h"
#include <imgui_internal.h>

#include "ProfileService.h"

namespace editor::profiler_view
{
	namespace
	{
		// 예산선. 60/30 FPS.
		constexpr double kBudget60 = 1000.0 / 60.0;
		constexpr double kBudget30 = 1000.0 / 30.0;

		// ★ 막대 폭은 **고정**이다. 폭을 프레임 수로 나누면 프레임이 쌓일수록
		//   막대가 얇아지고, 같은 프레임이 매 스냅샷 다른 자리로 옮겨 간다.
		//   그러면 그래프가 왼쪽으로 흐르는 것이 아니라 매번 다시 그려지는
		//   그림이 되어서, 방금 본 스파이크를 눈으로 좇을 수가 없다.
		constexpr float kBarWidthRatio = 0.20f;
		constexpr float kMinimumBarWidth = 3.0f;
		constexpr float kGraphHeightRows = 4.0f;

		// ── 왼쪽 눈금 칸 ───────────────────────────────────────────────────
		//
		// ★ ms 라벨을 그래프 **안**에 적으면 막대와 겹친다. 선 위에 두면
		//   맨 윗선의 글자가 프레임 번호 자 위로 나가고, 아래에 두면 막대에
		//   묻힌다 — 어느 쪽도 자리가 없다. 그래서 막대가 들어오지 않는
		//   칸을 왼쪽에 따로 낸다. 타임라인의 레인 머리글과 같은 방식이다.
		//
		// ★ 폭을 타임라인의 레인 머리글과 **같게** 잡는다. 두 그림이 같은 창을
		//   그리므로 같은 x 가 같은 자리여야 한다 — 왼쪽 여백이 다르면 위
		//   막대와 아래 구간이 어긋나 보이고, 그러면 눈으로 잇는 일이 다시
		//   사람 몫이 된다.
		constexpr float kScaleGutterWidth = 150.0f;

		// 세로 눈금은 이 계단에서 고른다. 최댓값에 딱 맞추면 프레임 하나가
		// 튈 때마다 눈금이 통째로 바뀌어 높이를 눈으로 비교할 수 없다.
		constexpr double kScaleSteps[] = { 16.67, 33.33, 50.0, 100.0, 200.0, 500.0, 1000.0 };

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

		// 보이는 구간의 최댓값을 덮는 가장 낮은 계단.
		double quantized_scale(double peak)
		{
			for (const double step : kScaleSteps)
			{
				if (peak <= step) { return step; }
			}
			return kScaleSteps[std::size(kScaleSteps) - 1];
		}
	}

	void draw_frame_overview()
	{
		ce::capture_reader& view = reader();
		const ce::capture_session* capture = view.capture();
		if (!capture || capture->frame_count() == 0)
		{
			ImGui::TextDisabled("아직 캡처가 없다 - Record 를 켜면 프레임이 선다");
			return;
		}

		const std::span<const ce::frame_record> frames = capture->frames();
		const std::uint32_t availableFirst = view.available_first();
		const std::uint32_t availableLast = view.available_last();
		const std::uint32_t availableCount = availableLast - availableFirst + 1;

		const float rowHeight = ImGui::GetTextLineHeight();
		const float barWidth = (std::max)(rowHeight * kBarWidthRatio, kMinimumBarWidth);
		const float width = (std::max)(ImGui::GetContentRegionAvail().x, 64.0f);
		const float gutter = kScaleGutterWidth;
		const float plotWidth = (std::max)(width - gutter, 32.0f);

		// 담을 수 있는 만큼만 보여 준다. 나머지는 왼쪽에 남아 있고, 굴려서 본다.
		view.set_graph_span(static_cast<std::uint32_t>(plotWidth / barWidth));
		const std::uint32_t windowFirst = view.graph_first();
		const std::uint32_t windowLast = view.graph_last();

		// 눈금은 **보이는 구간**으로 세운다. 보존 전체로 세우면 굴려서 조용한
		// 구간으로 가도 옛 스파이크에 눌려 막대가 전부 바닥에 붙는다.
		double peak = 0.0;
		for (const ce::frame_record& frame : frames)
		{
			if (frame.engine_frame < windowFirst || frame.engine_frame > windowLast) { continue; }
			peak = (std::max)(peak, ticks_to_milliseconds(frame.tick_end - frame.tick_begin));
		}
		const double scale = quantized_scale(peak);

		// ── 위쪽 자 — 프레임 번호 ─────────────────────────────────────────
		//
		// 막대만 보면 어느 프레임을 보고 있는지 알 수가 없다. 유니티가 그러듯
		// 그래프 위에 번호를 새긴다.
		const ImVec2 rulerOrigin = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(width, rowHeight));
		ImDrawList* draw = ImGui::GetWindowDrawList();

		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 size(width, rowHeight * kGraphHeightRows);

		ImGui::InvisibleButton("##ProfilerFrameOverview", size);

		// ★ 휠을 이 항목이 **가져간다.** 안 가져가면 그래프를 굴리면서 프로파일러
		//   창까지 같이 굴러서, 한 번 굴릴 때마다 툴바가 위로 사라진다.
		ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);

		const bool hovered = ImGui::IsItemHovered();
		const bool active = ImGui::IsItemActive();

		draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
		                    IM_COL32(24, 26, 30, 255));
		draw->AddRectFilled(origin, ImVec2(origin.x + gutter, origin.y + size.y),
		                    IM_COL32(32, 35, 42, 255));

		const float plotLeft = origin.x + gutter;
		auto frame_x = [&](std::uint32_t engineFrame) -> float
		{
			return plotLeft + static_cast<float>(engineFrame - windowFirst) * barWidth;
		};

		// 눈금 간격은 글자가 겹치지 않을 만큼 벌린다.
		const float labelWidth = ImGui::CalcTextSize("00000000").x * 1.8f;
		std::uint32_t tickStep = 10;
		while (static_cast<float>(tickStep) * barWidth < labelWidth) { tickStep *= 10; }
		for (std::uint32_t f = windowFirst - (windowFirst % tickStep); f <= windowLast; f += tickStep)
		{
			if (f < windowFirst) { continue; }
			const float x = frame_x(f);

			// ★ 오른쪽 끝에서 잘린 번호는 **틀린 번호로 읽힌다** — `16800` 이
			//   `1680` 으로 보인다. 자리가 없으면 아예 안 적는다.
			if (x + labelWidth > origin.x + size.x) { break; }

			draw->AddLine(ImVec2(x, rulerOrigin.y + rowHeight * 0.6f),
			              ImVec2(x, rulerOrigin.y + rowHeight), IM_COL32(150, 155, 165, 160));
			char label[24];
			std::snprintf(label, sizeof(label), "%u", f);
			draw->AddText(ImVec2(x + 3.0f, rulerOrigin.y), IM_COL32(170, 175, 185, 200), label);
			draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + size.y),
			              IM_COL32(255, 255, 255, 16));
		}

		// ── 막대 ─────────────────────────────────────────────────────────
		for (const ce::frame_record& frame : frames)
		{
			if (frame.engine_frame < windowFirst || frame.engine_frame > windowLast) { continue; }

			const double milliseconds =
				ticks_to_milliseconds(frame.tick_end - frame.tick_begin);
			const float normalized = static_cast<float>(
				(std::min)(milliseconds / scale, 1.0));
			const float height = normalized * size.y;

			const float x0 = frame_x(frame.engine_frame);
			const float x1 = x0 + (std::max)(barWidth - 1.0f, 1.0f);
			const bool selected = frame.engine_frame >= view.selected_first()
				&& frame.engine_frame <= view.selected_last();

			draw->AddRectFilled(ImVec2(x0, origin.y + size.y - height),
			                    ImVec2(x1, origin.y + size.y),
			                    bar_color(milliseconds, selected));
		}

		// 예산선과 눈금 최댓값.
		//
		// ★ 선은 다 긋되 **글자는 자리가 있을 때만** 적는다. 그래프가 네 줄
		//   높이인데 16.67 과 33.33 은 그 안에서 한 줄 거리도 안 떨어져서,
		//   그냥 적으면 둘이 겹쳐 둘 다 못 읽는다. 글자를 선 **아래**에
		//   두는 것도 같은 까닭이다 — 위에 두면 맨 윗선의 글자가 그래프를
		//   벗어나 프레임 번호 자 위에 얹힌다.
		const float labelHeight = ImGui::GetTextLineHeight();
		float lastLabelY = -1000.0f;
		for (const double level : { scale, kBudget30, kBudget60 })
		{
			if (level > scale)
			{
				continue;
			}
			const float y = origin.y + size.y -
				static_cast<float>(level / scale) * size.y;
			draw->AddLine(ImVec2(plotLeft, y), ImVec2(origin.x + size.x, y),
			              IM_COL32(200, 200, 200, 90));

			if (y - lastLabelY < labelHeight)
			{
				continue;
			}
			lastLabelY = y;

			// 선 높이에 글자 가운데를 맞춘다. 칸 안이라 위아래 어느 쪽으로
			// 삐져나가도 막대를 가리지 않는다.
			char label[32];
			std::snprintf(label, sizeof(label), "%.4g ms", level);
			const float textY = (std::clamp)(y - labelHeight * 0.5f,
			                                 origin.y, origin.y + size.y - labelHeight);
			draw->AddText(ImVec2(origin.x + 4.0f, textY),
			              IM_COL32(200, 200, 200, 170), label);
		}

		// 마우스 → 프레임.
		auto frame_at = [&](float mouseX) -> std::uint32_t
		{
			const float local = (std::max)(mouseX - plotLeft, 0.0f);
			const std::uint32_t offset = static_cast<std::uint32_t>(local / barWidth);
			return (std::min)(windowFirst + offset, windowLast);
		};
		auto record_of = [&](std::uint32_t engineFrame) -> const ce::frame_record*
		{
			if (engineFrame < availableFirst || engineFrame > availableLast) { return nullptr; }

			// 번호가 이어진다고 **믿지 않는다.** 링이 프레임을 건너뛴 적이
			// 있으면 색인 계산이 남의 프레임을 가리키고, 그때 말풍선은
			// 아무 말도 안 하는 대신 **틀린 수**를 낸다.
			const std::size_t index = engineFrame - availableFirst;
			if (index < frames.size() && frames[index].engine_frame == engineFrame)
			{
				return &frames[index];
			}
			for (const ce::frame_record& frame : frames)
			{
				if (frame.engine_frame == engineFrame) { return &frame; }
			}
			return nullptr;
		};

		if (hovered || active)
		{
			if (const ce::frame_record* frame = record_of(frame_at(ImGui::GetIO().MousePos.x)))
			{
				ImGui::SetTooltip("frame %u\n%.3f ms\n이벤트 %zu",
				                  frame->engine_frame,
				                  ticks_to_milliseconds(frame->tick_end - frame->tick_begin),
				                  frame->events.size());
			}

			// 휠로 굴린다. 한 번에 창의 1/8 — 한 프레임씩이면 600 프레임을
			// 되짚는 데 손목이 남아나지 않는다.
			//
			// ★ 굴린 양이 아니라 **방향**만 본다. 터치패드는 0.3 같은 값을
			//   내는데, 그것을 정수로 자르면 0 이 되어 아무 일도 안 일어난다.
			const float wheel = ImGui::GetIO().MouseWheel;
			if (wheel != 0.0f)
			{
				const std::int32_t step = (std::max)(
					static_cast<std::int32_t>(view.graph_count() / 8), 1);
				view.pan_graph((wheel > 0.0f) ? -step : step);
			}
		}

		// ★ 고르는 순간 따라가기를 끈다. 그러지 않으면 다음 캡처가 올 때
		//   선택이 최신으로 튀어, 붙잡아 둔 프레임이 손에서 빠져나간다.
		if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			view.set_live_follow(false);
			view.select_range(frame_at(ImGui::GetIO().MouseClickedPos[0].x),
			                  frame_at(ImGui::GetIO().MousePos.x));
		}
		else if (ImGui::IsItemDeactivated() && !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left))
		{
			view.set_live_follow(false);
			view.select_frame(frame_at(ImGui::GetIO().MousePos.x));
		}

		// ── 가로 스크롤 막대 ─────────────────────────────────────────────
		//
		// 보존 구간 전체에서 지금 어디를 보고 있는지, 끌어서 옮긴다.
		const float scrollHeight = (std::max)(rowHeight * 0.4f, 8.0f);
		ImGui::Dummy(ImVec2(gutter, scrollHeight));
		ImGui::SameLine(0.0f, 0.0f);
		const ImVec2 scrollOrigin = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("##ProfilerFrameScroll", ImVec2(plotWidth, scrollHeight));
		draw->AddRectFilled(scrollOrigin,
		                    ImVec2(scrollOrigin.x + plotWidth, scrollOrigin.y + scrollHeight),
		                    IM_COL32(18, 20, 24, 255));

		const float visibleRatio = static_cast<float>(view.graph_count())
			/ static_cast<float>(availableCount);
		const float startRatio = static_cast<float>(windowFirst - availableFirst)
			/ static_cast<float>(availableCount);
		const float handleX = scrollOrigin.x + startRatio * plotWidth;
		const float handleW = (std::max)(visibleRatio * plotWidth, 12.0f);
		draw->AddRectFilled(ImVec2(handleX, scrollOrigin.y),
		                    ImVec2(handleX + handleW, scrollOrigin.y + scrollHeight),
		                    view.live_follow() ? IM_COL32(110, 160, 200, 220)
		                                       : IM_COL32(200, 170, 110, 220));

		if (ImGui::IsItemActive())
		{
			// 손잡이 가운데를 마우스에 맞춘다.
			const float local = ImGui::GetIO().MousePos.x - scrollOrigin.x - handleW * 0.5f;
			const float target = (std::max)(local, 0.0f) / plotWidth * static_cast<float>(availableCount);
			const std::int32_t delta = static_cast<std::int32_t>(
				availableFirst + static_cast<std::uint32_t>(target)) -
				static_cast<std::int32_t>(windowFirst);
			view.pan_graph(delta);
		}

		// 보존 구간과 선택을 글로도 낸다. 그래프만으로는 어느 프레임인지
		// 읽을 수 없다.
		if (view.selected_count() > 1)
		{
			ImGui::Text("frames %u..%u (%u개)  ·  보는 중 %u..%u  ·  보존 %u..%u",
			            view.selected_first(), view.selected_last(), view.selected_count(),
			            windowFirst, windowLast, availableFirst, availableLast);
		}
		else
		{
			ImGui::Text("frame %u  ·  보는 중 %u..%u  ·  보존 %u..%u",
			            view.selected_first(),
			            windowFirst, windowLast, availableFirst, availableLast);
		}

		if (!view.live_follow())
		{
			ImGui::SameLine();
			if (ImGui::SmallButton("최신으로"))
			{
				view.set_live_follow(true);
			}
		}
	}
}
