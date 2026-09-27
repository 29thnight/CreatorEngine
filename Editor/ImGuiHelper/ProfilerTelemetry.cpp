// P5 telemetry view. All values come from the immutable capture; the UI never
// asks the renderer or process for live state while drawing.
#include "ProfilerView.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "ImGui.h"
#include "ProfileCapture.h"
#include "ProfileScope.h"

namespace editor::profiler_view
{
	namespace
	{
		enum class metric : std::uint8_t
		{
			cpu, ram, gpu_busy, vram, lan_send, lan_receive
		};
		struct metric_descriptor
		{
			metric id;
			const char* name;
			const char* unit;
			const char* source;
			ImU32 color;
			std::uint32_t max_gap;
		};
		struct plotted_sample
		{
			std::uint32_t frame;
			ce::profile_tick tick;
			double value;
		};
		struct gpu_series_cache
		{
			std::weak_ptr<const ce::capture_session> capture;
			std::vector<std::optional<double>> values;
			std::vector<std::uint8_t> ready;
			std::vector<std::pair<ce::profile_tick, ce::profile_tick>> scratch;
		};
		constexpr metric_descriptor kMetrics[] = {
			{ metric::cpu, "CPU", "%", "엔진 프로세스 / 전체 논리 CPU", IM_COL32(112, 191, 255, 255), 1 },
			{ metric::ram, "RAM", "MB", "엔진 프로세스 작업 집합", IM_COL32(122, 218, 157, 255), 1 },
			{ metric::gpu_busy, "GPU Graphics", "ms", "캡처된 Graphics 큐 구간의 합집합", IM_COL32(231, 177, 105, 255), 1 },
			{ metric::vram, "VRAM", "MB", "렌더 백엔드의 비디오 메모리 사용량", IM_COL32(201, 149, 236, 255), 0 },
			{ metric::lan_send, "LAN 송신", "B/s", "엔진 통신 계측원", IM_COL32(236, 143, 139, 255), 1 },
			{ metric::lan_receive, "LAN 수신", "B/s", "엔진 통신 계측원", IM_COL32(149, 200, 230, 255), 1 },
		};

		std::optional<double> counter_value(const ce::frame_record& frame, ce::profile_counter_id id)
		{
			for (const auto& sample : frame.counters)
				if (sample.id == id) return sample.value;
			return std::nullopt;
		}

		std::optional<double> gpu_busy_ms(const ce::capture_session& capture,
		                                  const ce::frame_record& frame,
		                                  std::vector<std::pair<ce::profile_tick, ce::profile_tick>>& spans)
		{
			spans.clear();
			for (const ce::profile_event& event : frame.events)
			{
				if (ce::has_flag(event.flags, ce::event_flags::gpu_span) &&
				    event.queue == 0 && event.tick_end >= event.tick_begin)
					spans.emplace_back(event.tick_begin, event.tick_end);
			}
			if (spans.empty()) return std::nullopt;
			std::sort(spans.begin(), spans.end());
			ce::profile_tick begin = spans.front().first;
			ce::profile_tick end = spans.front().second;
			ce::profile_tick busy = 0;
			for (std::size_t i = 1; i < spans.size(); ++i)
			{
				if (spans[i].first <= end) end = (std::max)(end, spans[i].second);
				else
				{
					busy += end - begin;
					begin = spans[i].first;
					end = spans[i].second;
				}
			}
			return capture.milliseconds(busy + end - begin);
		}

		std::optional<double> value_of(const ce::capture_session& capture,
		                               const ce::frame_record& frame, metric id,
		                               gpu_series_cache& cache, std::size_t frameIndex)
		{
			switch (id)
			{
			case metric::cpu: return counter_value(frame, ce::profile_counter_id::process_cpu_percent);
			case metric::ram: return counter_value(frame, ce::profile_counter_id::process_ram_mb);
			case metric::gpu_busy:
				if (!cache.ready[frameIndex])
				{
					cache.values[frameIndex] = gpu_busy_ms(capture, frame, cache.scratch);
					cache.ready[frameIndex] = 1;
				}
				return cache.values[frameIndex];
			case metric::vram: return counter_value(frame, ce::profile_counter_id::gpu_vram_mb);
			case metric::lan_send: return counter_value(frame, ce::profile_counter_id::lan_send_bytes_per_second);
			case metric::lan_receive: return counter_value(frame, ce::profile_counter_id::lan_receive_bytes_per_second);
			}
			return std::nullopt;
		}

		void draw_metric(const ce::capture_session& capture, const metric_descriptor& descriptor,
		                 gpu_series_cache& cache, float graphHeight)
		{
			ce::capture_reader& view = reader();
			const std::uint32_t first = view.graph_first();
			const std::uint32_t last = view.graph_last();
			const std::uint32_t selectedFirst = view.selected_first();
			const std::uint32_t selectedLast = view.selected_last();
			double minValue = 0.0, maxValue = 0.0, sum = 0.0;
			std::uint32_t selectedCount = 0;
			bool haveScale = false;
			std::vector<plotted_sample> points;
			std::vector<double> selectedValues;
			points.reserve((std::min)(capture.frame_count(), last - first + 1));
			const auto frames = capture.frames();
			for (std::size_t i = 0; i < frames.size(); ++i)
			{
				const ce::frame_record& frame = frames[i];
				if (frame.engine_frame < first || frame.engine_frame > last) continue;
				const auto value = value_of(capture, frame, descriptor.id, cache, i);
				if (!value || !std::isfinite(*value)) continue;
				points.push_back({ frame.engine_frame, frame.tick_end, *value });
				if (!haveScale) { minValue = maxValue = *value; haveScale = true; }
				else { minValue = (std::min)(minValue, *value); maxValue = (std::max)(maxValue, *value); }
				if (frame.engine_frame >= selectedFirst && frame.engine_frame <= selectedLast)
				{
					sum += *value;
					++selectedCount;
					selectedValues.push_back(*value);
				}
			}
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(descriptor.color), "%s", descriptor.name);
			ImGui::SameLine();
			if (selectedCount)
			{
				std::sort(selectedValues.begin(), selectedValues.end());
				const double p95 = selectedValues[static_cast<std::size_t>(
					std::ceil(selectedValues.size() * 0.95)) - 1];
				ImGui::TextDisabled("선택 %u표본  평균 %.2f  P95 %.2f  최소 %.2f  최대 %.2f %s",
				                    selectedCount, sum / selectedCount, p95,
				                    selectedValues.front(), selectedValues.back(), descriptor.unit);
			}
			else ImGui::TextDisabled("선택 구간에 표본 없음");
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", descriptor.source);

			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const ImVec2 size((std::max)(ImGui::GetContentRegionAvail().x, 80.0f), graphHeight);
			ImGui::PushID(static_cast<int>(descriptor.id));
			ImGui::InvisibleButton("##TelemetryGraph", size);
			const bool hovered = ImGui::IsItemHovered();
			ImDrawList* draw = ImGui::GetWindowDrawList();
			draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
			                    IM_COL32(24, 27, 33, 255), 3.0f);
			const float left = origin.x + 54.0f;
			const float right = origin.x + size.x - 8.0f;
			const float top = origin.y + 8.0f;
			const float bottom = origin.y + size.y - 8.0f;
			for (int line = 0; line <= 2; ++line)
			{
				const float y = top + (bottom - top) * line / 2.0f;
				draw->AddLine(ImVec2(left, y), ImVec2(right, y), IM_COL32(105, 112, 123, 50));
			}
			if (!haveScale)
			{
				const char* reason = descriptor.id == metric::lan_send || descriptor.id == metric::lan_receive
					? "엔진 네트워크 계측원 없음" : "이 구간에 측정값 없음";
				draw->AddText(ImVec2(left + 8.0f, origin.y + 28.0f),
				              IM_COL32(170, 177, 186, 220), reason);
				ImGui::PopID();
				return;
			}
			// Memory uses a local range so a small leak is not flattened by zero.
			const double padding = (std::max)((maxValue - minValue) * 0.15, 0.01);
			const double floor = (descriptor.id == metric::ram || descriptor.id == metric::vram)
				? minValue - padding : 0.0;
			const double ceiling = (std::max)(maxValue + padding, floor + 0.01);
			char label[40];
			std::snprintf(label, sizeof(label), "%.1f", ceiling);
			draw->AddText(ImVec2(origin.x + 4.0f, top), IM_COL32(164, 171, 181, 210), label);
			std::snprintf(label, sizeof(label), "%.1f", floor);
			draw->AddText(ImVec2(origin.x + 4.0f, bottom - ImGui::GetTextLineHeight()),
			              IM_COL32(164, 171, 181, 210), label);
			const double span = static_cast<double>((std::max)(last - first, 1u));
			bool previousValid = false;
			ImVec2 previous{};
			std::uint32_t previousFrame = 0;
			ce::profile_tick previousTick = 0;
			std::uint32_t hoveredFrame = 0;
			double hoveredValue = 0.0;
			float hoveredDistance = 12.0f;
			for (const plotted_sample& sample : points)
			{
				const std::uint32_t frame = sample.frame;
				const double value = sample.value;
				const float x = left + static_cast<float>((frame - first) / span) * (right - left);
				const float y = bottom - static_cast<float>((value - floor) / (ceiling - floor)) * (bottom - top);
				const ImVec2 point(x, y);
				const bool connected = descriptor.id == metric::vram
					? (sample.tick >= previousTick &&
					   capture.milliseconds(sample.tick - previousTick) <= 550.0)
					: frame - previousFrame <= descriptor.max_gap;
				if (previousValid && connected)
					draw->AddLine(previous, point, descriptor.color, 1.8f);
				if (frame >= selectedFirst && frame <= selectedLast)
					draw->AddCircleFilled(point, 2.5f, descriptor.color);
				if (hovered && std::abs(ImGui::GetIO().MousePos.x - x) < hoveredDistance)
				{
					hoveredDistance = std::abs(ImGui::GetIO().MousePos.x - x);
					hoveredFrame = frame;
					hoveredValue = value;
				}
				previous = point;
				previousFrame = frame;
				previousTick = sample.tick;
				previousValid = true;
			}
			if (hovered && hoveredFrame)
			{
				ImGui::SetTooltip("frame %u  |  %.3f %s\n%s", hoveredFrame,
				                  hoveredValue, descriptor.unit, descriptor.source);
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					view.set_live_follow(false);
					view.select_frame(hoveredFrame);
					view.focus_frame(hoveredFrame);
				}
			}
			ImGui::PopID();
		}

		void draw_context(const ce::capture_session& capture, telemetry_page page)
		{
			const ce::capture_reader& view = reader();
			ImGui::TextUnformatted("선택 구간");
			ImGui::Separator();
			ImGui::Text("frame %u..%u", view.selected_first(), view.selected_last());
			ImGui::TextDisabled("보이는 구간 %u..%u", view.graph_first(), view.graph_last());
			ImGui::Spacing();
			if (page == telemetry_page::cpu)
			{
				ImGui::TextUnformatted("CPU Self 상위 마커");
				ImGui::Separator();
				std::vector<const ce::aggregate_row*> ranked;
				for (const auto& row : view.aggregate().flat())
					if (row.self_ticks > 0) ranked.push_back(&row);
				std::sort(ranked.begin(), ranked.end(), [](const auto* a, const auto* b)
					{ return a->self_ticks > b->self_ticks; });
				const std::size_t count = (std::min)(ranked.size(), std::size_t{ 8 });
				for (std::size_t i = 0; i < count; ++i)
				{
					const auto* row = ranked[i];
					ImGui::PushID(static_cast<int>(i));
					ImGui::Text("%zu. %.3f ms", i + 1, capture.milliseconds(row->self_ticks));
					ImGui::TextDisabled("%s", marker_name(&capture, row->marker));
					ImGui::PopID();
				}
				if (count == 0) ImGui::TextDisabled("선택 구간에 CPU 마커 없음");
			}
			else if (page == telemetry_page::memory)
			{
				ImGui::TextWrapped("RAM은 엔진 프로세스의 작업 집합입니다. VRAM은 GPU 페이지에서 렌더 백엔드가 발행한 표본으로 봅니다.");
			}
			else if (page == telemetry_page::gpu)
			{
				ImGui::TextWrapped("Graphics 시간은 캡처된 GPU 구간의 합집합입니다. GPU 사용률(%%)과는 다른 값입니다.");
				ImGui::Spacing();
				ImGui::TextDisabled("GPU 패스의 자세한 위치는 Timeline 탭에서 확인합니다.");
			}
			else
			{
				ImGui::TextWrapped("LAN은 엔진이 직접 보낸·받은 바이트만 계상합니다. 계측원이 연결되기 전에는 값을 0으로 그리지 않습니다.");
			}
			ImGui::Spacing();
			ImGui::TextUnformatted("프레임 Counter");
			ImGui::Separator();
			const ce::frame_record* frame = capture.find_frame(view.selected_last());
			if (!frame)
			{
				ImGui::TextDisabled("선택한 프레임에 값 없음");
				return;
			}
			bool any = false;
			for (const auto& descriptor : capture.counter_descriptors())
			{
				const bool relevant = page == telemetry_page::cpu
					? descriptor.category == ce::counter_category::process
					: page == telemetry_page::memory
					? descriptor.category == ce::counter_category::managed || descriptor.category == ce::counter_category::resources
					: page == telemetry_page::gpu
					? descriptor.category == ce::counter_category::gpu || descriptor.category == ce::counter_category::render
					: descriptor.category == ce::counter_category::network;
				if (!relevant) continue;
				const auto value = counter_value(*frame, descriptor.id);
				if (!value) continue;
				any = true;
				ImGui::Text("%s  %.2f %s", descriptor.name.c_str(), *value, descriptor.unit.c_str());
			}
			if (!any) ImGui::TextDisabled("선택한 프레임에 해당 모듈의 표본 없음");
		}

		void draw_cpu_comparison(const ce::capture_session& capture)
		{
			const ce::capture_reader& view = reader();
			const std::uint32_t aFirst = view.selected_first();
			const std::uint32_t aLast = view.selected_last();
			const std::uint32_t availableFirst = view.available_first();
			const std::uint32_t availableLast = view.available_last();
			static std::uint32_t bFirst = 0, bLast = 0;
			static std::uint32_t previousAFirst = 0, previousALast = 0;
			if (aFirst != previousAFirst || aLast != previousALast || bFirst == 0)
			{
				const std::uint32_t width = aLast - aFirst + 1;
				bLast = aFirst > availableFirst ? aFirst - 1 : aFirst;
				bFirst = bLast + 1 > width ? bLast + 1 - width : availableFirst;
				previousAFirst = aFirst;
				previousALast = aLast;
			}
			bFirst = (std::clamp)(bFirst, availableFirst, availableLast);
			bLast = (std::clamp)(bLast, availableFirst, availableLast);
			const auto average = [&](std::uint32_t first, std::uint32_t last)
			{
				double total = 0.0;
				std::uint32_t count = 0;
				for (const ce::frame_record& frame : capture.frames())
				{
					if (frame.engine_frame < first || frame.engine_frame > last) continue;
					const auto value = counter_value(frame, ce::profile_counter_id::process_cpu_percent);
					if (value && std::isfinite(*value)) { total += *value; ++count; }
				}
				return std::pair{ count, count ? total / count : 0.0 };
			};
			const auto [aCount, aAverage] = average(aFirst, aLast);
			const auto [bCount, bAverage] = average(bFirst, bLast);
			if (ImGui::BeginTable("##CpuCompare", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable))
			{
				ImGui::TableNextColumn();
				ImGui::TextColored(ImVec4(0.42f, 0.70f, 1.0f, 1.0f), "A · 선택 구간");
				ImGui::Text("frame %u..%u", aFirst, aLast);
				if (aCount) ImGui::Text("평균 CPU %.2f%% · %u표본", aAverage, aCount);
				else ImGui::TextDisabled("CPU 표본 없음 · 구간을 넓혀 보세요");
				ImGui::TableNextColumn();
				ImGui::TextColored(ImVec4(0.70f, 0.52f, 0.93f, 1.0f), "B · 비교 구간");
				ImGui::SetNextItemWidth(125.0f);
				ImGui::InputScalar("시작##CpuB", ImGuiDataType_U32, &bFirst);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(125.0f);
				ImGui::InputScalar("끝##CpuB", ImGuiDataType_U32, &bLast);
				if (bCount) ImGui::Text("평균 CPU %.2f%% · %u표본", bAverage, bCount);
				else ImGui::TextDisabled("CPU 표본 없음 · 비교 구간을 넓혀 보세요");
				if (aCount && bCount)
					ImGui::Text("A - B  %+.2f%%p", aAverage - bAverage);
				ImGui::EndTable();
			}
		}
	}

	void draw_telemetry(telemetry_page page)
	{
		const ce::capture_session* capture = reader().capture();
		if (!capture || capture->frame_count() == 0)
		{
			ImGui::TextDisabled("아직 캡처가 없습니다. Record를 켜면 이 페이지에 기록이 나타납니다.");
			return;
		}
		ce::profile_scope telemetry{ ce::marker<"ProfilerTelemetry">() };
		static gpu_series_cache gpuCache;
		if (gpuCache.capture.lock().get() != capture)
		{
			gpuCache.capture = reader().capture_handle();
			gpuCache.values.assign(capture->frame_count(), std::nullopt);
			gpuCache.ready.assign(capture->frame_count(), 0);
			gpuCache.scratch.clear();
		}
		const char* title = page == telemetry_page::cpu ? "CPU 사용률" :
			page == telemetry_page::memory ? "프로세스 메모리" :
			page == telemetry_page::gpu ? "GPU Graphics · VRAM" : "엔진 네트워크";
		ImGui::TextUnformatted(title);
		ImGui::SameLine();
		ImGui::TextDisabled("| 프레임을 클릭하면 모든 페이지의 선택 구간이 바뀝니다");
		ImGui::Separator();
		if (page == telemetry_page::cpu)
		{
			draw_cpu_comparison(*capture);
			ImGui::Spacing();
		}
		const bool split = ImGui::GetContentRegionAvail().x >= 900.0f &&
			ImGui::BeginTable("##TelemetryLayout", 2, ImGuiTableFlags_Resizable);
		if (split)
		{
			ImGui::TableSetupColumn("Timeseries", ImGuiTableColumnFlags_WidthStretch, 0.72f);
			ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch, 0.28f);
			ImGui::TableNextColumn();
		}
		if (page == telemetry_page::cpu) draw_metric(*capture, kMetrics[0], gpuCache, 240.0f);
		else if (page == telemetry_page::memory) draw_metric(*capture, kMetrics[1], gpuCache, 240.0f);
		else if (page == telemetry_page::gpu)
		{
			draw_metric(*capture, kMetrics[2], gpuCache, 190.0f);
			ImGui::Spacing();
			draw_metric(*capture, kMetrics[3], gpuCache, 160.0f);
		}
		else
		{
			bool available = false;
			for (const ce::frame_record& frame : capture->frames())
			{
				available = counter_value(frame, ce::profile_counter_id::lan_send_bytes_per_second)
					|| counter_value(frame, ce::profile_counter_id::lan_receive_bytes_per_second);
				if (available) break;
			}
			if (available)
			{
				draw_metric(*capture, kMetrics[4], gpuCache, 190.0f);
				ImGui::Spacing();
				draw_metric(*capture, kMetrics[5], gpuCache, 190.0f);
			}
			else ImGui::TextDisabled("LAN 송신/수신: 측정 불가 (엔진 네트워크 계측원 없음)");
		}
		if (split) ImGui::TableNextColumn();
		else ImGui::Separator();
		draw_context(*capture, page);
		if (split) ImGui::EndTable();
	}

	void draw_telemetry_dashboard()
	{
		const ce::capture_session* capture = reader().capture();
		if (!capture || capture->frame_count() == 0)
		{
			ImGui::TextDisabled("아직 캡처가 없습니다. Record를 켜면 프레임 그래프가 나타납니다.");
			return;
		}
		static gpu_series_cache gpuCache;
		if (gpuCache.capture.lock().get() != capture)
		{
			gpuCache.capture = reader().capture_handle();
			gpuCache.values.assign(capture->frame_count(), std::nullopt);
			gpuCache.ready.assign(capture->frame_count(), 0);
			gpuCache.scratch.clear();
		}
		ImGui::TextDisabled("선택한 프레임 범위를 모든 그래프에 적용합니다. 값을 클릭하면 해당 프레임으로 이동합니다.");
		const bool columns = ImGui::GetContentRegionAvail().x >= 850.0f &&
			ImGui::BeginTable("##ProfilerDashboard", 2, ImGuiTableFlags_Resizable);
		if (columns) ImGui::TableNextColumn();
		draw_metric(*capture, kMetrics[0], gpuCache, 160.0f);
		if (columns) ImGui::TableNextColumn(); else ImGui::Spacing();
		draw_metric(*capture, kMetrics[1], gpuCache, 160.0f);
		if (columns) ImGui::TableNextRow(); else ImGui::Spacing();
		if (columns) ImGui::TableNextColumn();
		draw_metric(*capture, kMetrics[2], gpuCache, 160.0f);
		if (columns) ImGui::TableNextColumn(); else ImGui::Spacing();
		draw_metric(*capture, kMetrics[3], gpuCache, 160.0f);
		if (columns) ImGui::EndTable();
		ImGui::TextDisabled("LAN: 엔진 네트워크 계측원이 연결되면 같은 프레임 기준으로 추가됩니다.");
	}
}
