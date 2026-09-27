#include "ProfilerView.h"

#include "MemoryProfilerSnapshot.h"
#include "EditorIcons.h"
#include "ImGui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace editor::profiler_view
{
	namespace
	{
		using memory_profiler::object_entry;
		using memory_profiler::object_kind;
		using memory_profiler::snapshot;
		const char* kind_name(object_kind kind);

		struct memory_view_state
		{
			std::vector<std::shared_ptr<const snapshot>> snapshots;
			std::uint64_t last_seen{};
			int selected_a{ -1 };
			int selected_b{ -1 };
			char search[128]{};
			int region_filter{};
			std::uint64_t selected_region_address{};
		};

		memory_view_state& state()
		{
			static memory_view_state value;
			return value;
		}

		void refresh(memory_view_state& view)
		{
			const auto latest = memory_profiler::snapshot_service::instance().latest();
			if (!latest || latest->serial == view.last_seen) return;
			view.last_seen = latest->serial;
			view.snapshots.push_back(latest);
			if (view.snapshots.size() > 8)
			{
				view.snapshots.erase(view.snapshots.begin());
				if (view.selected_a >= 0) --view.selected_a;
				if (view.selected_b >= 0) --view.selected_b;
			}
			view.selected_b = view.selected_a;
			view.selected_a = static_cast<int>(view.snapshots.size()) - 1;
		}

		const snapshot* selected(const memory_view_state& view, int index)
		{
			return index >= 0 && index < static_cast<int>(view.snapshots.size())
				? view.snapshots[index].get() : nullptr;
		}

		void select_snapshot(const char* label, const memory_view_state& view, int& index, bool allowNone)
		{
			char preview[64]{};
			if (const snapshot* current = selected(view, index))
				std::snprintf(preview, sizeof(preview), "#%llu · frame %u",
					static_cast<unsigned long long>(current->serial), current->frame);
			else std::snprintf(preview, sizeof(preview), "없음");
			ImGui::SetNextItemWidth(220.0f);
			if (!ImGui::BeginCombo(label, preview)) return;
			if (allowNone && ImGui::Selectable("비교 안 함", index < 0)) index = -1;
			for (int i = static_cast<int>(view.snapshots.size()) - 1; i >= 0; --i)
			{
				const auto& item = *view.snapshots[i];
				char option[72]{};
				std::snprintf(option, sizeof(option), "#%llu · frame %u",
					static_cast<unsigned long long>(item.serial), item.frame);
				if (ImGui::Selectable(option, i == index)) index = i;
			}
			ImGui::EndCombo();
		}

		std::string bytes_label(std::uint64_t bytes)
		{
			char text[48]{};
			if (bytes >= 1024ull * 1024ull * 1024ull)
				std::snprintf(text, sizeof(text), "%.2f GiB", bytes / 1073741824.0);
			else if (bytes >= 1024ull * 1024ull)
				std::snprintf(text, sizeof(text), "%.2f MiB", bytes / 1048576.0);
			else if (bytes >= 1024ull)
				std::snprintf(text, sizeof(text), "%.1f KiB", bytes / 1024.0);
			else std::snprintf(text, sizeof(text), "%llu B", static_cast<unsigned long long>(bytes));
			return text;
		}

		void metric_row(const char* name, std::uint64_t value, bool available,
		                const snapshot* other = nullptr, std::uint64_t otherValue = 0,
		                bool otherAvailable = false)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(name);
			ImGui::TableNextColumn();
			if (available) ImGui::TextUnformatted(bytes_label(value).c_str());
			else ImGui::TextDisabled("미계측");
			if (other)
			{
				ImGui::TableNextColumn();
				if (otherAvailable) ImGui::TextUnformatted(bytes_label(otherValue).c_str());
				else ImGui::TextDisabled("미계측");
				ImGui::TableNextColumn();
				if (available && otherAvailable)
					ImGui::Text("%+.2f MiB", (static_cast<double>(value) - otherValue) / 1048576.0);
				else ImGui::TextDisabled("-");
			}
		}

		struct bar_part
		{
			const char* label;
			std::uint64_t bytes;
			ImU32 color;
		};
		constexpr float kSummaryOuterMargin = 18.0f;
		constexpr float kSummaryBarHeight = 42.0f;

		bool begin_summary_card(const char* id)
		{
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + kSummaryOuterMargin);
			const float width = (std::max)(1.0f,
				ImGui::GetContentRegionAvail().x - kSummaryOuterMargin);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f, 16.0f));
			return ImGui::BeginChild(id, ImVec2(width, 0.0f),
				ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
		}

		void end_summary_card()
		{
			ImGui::EndChild();
			ImGui::PopStyleVar();
		}

		void summary_card_gap()
		{
			ImGui::Dummy(ImVec2(0.0f, 12.0f));
		}

		void segmented_bar(const char* id, std::span<const bar_part> parts, std::uint64_t total)
		{
			ImGui::PushID(id);
			const ImVec2 at = ImGui::GetCursorScreenPos();
			const float width = ImGui::GetContentRegionAvail().x;
			if (width <= 0.0f) { ImGui::PopID(); return; }
			ImGui::InvisibleButton("##bar", ImVec2(width, kSummaryBarHeight));
			ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + width, at.y + kSummaryBarHeight),
				ImGui::GetColorU32(ImGuiCol_FrameBg));
			float x = at.x;
			const float mouseX = ImGui::GetMousePos().x;
			int hoveredPart = -1;
			if (total > 0) for (std::size_t i = 0; i < parts.size(); ++i)
			{
				const float end = i + 1 == parts.size() ? at.x + width :
					x + width * static_cast<float>(static_cast<double>(parts[i].bytes) / total);
				if (end > x)
					ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x, at.y),
						ImVec2((std::max)(x, end - 3.0f), at.y + kSummaryBarHeight), parts[i].color);
				if (mouseX >= x && mouseX < end) hoveredPart = static_cast<int>(i);
				x = end;
			}
			ImGui::GetWindowDrawList()->AddRect(at, ImVec2(at.x + width, at.y + kSummaryBarHeight),
				ImGui::GetColorU32(ImGuiCol_Border));
			if (ImGui::IsItemHovered() && hoveredPart >= 0)
			{
				const auto& part = parts[static_cast<std::size_t>(hoveredPart)];
				ImGui::SetTooltip("%s · %s (%.1f%%)", part.label, bytes_label(part.bytes).c_str(),
					100.0 * static_cast<double>(part.bytes) / total);
			}
			const std::string totalText = "합계 " + bytes_label(total);
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f,
				ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(totalText.c_str()).x));
			ImGui::TextDisabled("%s", totalText.c_str());
			if (ImGui::BeginTable("##legend", 2, ImGuiTableFlags_SizingFixedFit |
				ImGuiTableFlags_BordersInnerH))
			{
				ImGui::TableSetupColumn("항목", ImGuiTableColumnFlags_WidthFixed, 210.0f);
				ImGui::TableSetupColumn("크기", ImGuiTableColumnFlags_WidthFixed, 190.0f);
				for (const bar_part& part : parts)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 22.0f);
					ImGui::TextUnformatted(part.label);
					const ImVec2 textMin = ImGui::GetItemRectMin();
					const ImVec2 textMax = ImGui::GetItemRectMax();
					constexpr float swatchSize = 12.0f;
					const ImVec2 swatchMin(textMin.x - 18.0f,
						textMin.y + (textMax.y - textMin.y - swatchSize) * 0.5f);
					ImGui::GetWindowDrawList()->AddRectFilled(swatchMin,
						ImVec2(swatchMin.x + swatchSize, swatchMin.y + swatchSize), part.color);
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(bytes_label(part.bytes).c_str());
				}
				ImGui::EndTable();
			}
			ImGui::PopID();
		}

		void metric_tile(const char* title, const char* explanation,
			std::uint64_t value, bool available, const ImVec4& color,
			const snapshot* b, std::uint64_t previous, bool previousAvailable,
			std::uint64_t budget = 0)
		{
			ImGui::TextDisabled("%s", title);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", explanation);
			if (available) ImGui::TextColored(color, "%s", bytes_label(value).c_str());
			else ImGui::TextDisabled("미계측");
			if (available && budget > 0)
			{
				const float fraction = static_cast<float>((std::min)(1.0,
					static_cast<double>(value) / budget));
				ImGui::ProgressBar(fraction, ImVec2(-1.0f, 8.0f), "");
				ImGui::TextDisabled("예산 %s · %.1f%%", bytes_label(budget).c_str(),
					100.0 * static_cast<double>(value) / budget);
			}
			if (b)
			{
				if (previousAvailable)
				{
					ImGui::TextDisabled("B %s", bytes_label(previous).c_str());
					if (available)
					{
						const double delta = (static_cast<double>(value) - previous) / 1048576.0;
						ImGui::TextColored(delta > 0.0 ? ImVec4(0.96f, 0.58f, 0.50f, 1.0f) :
							ImVec4(0.53f, 0.79f, 0.65f, 1.0f), "A - B %+.2f MiB", delta);
					}
					else ImGui::TextDisabled("A - B 미계측");
				}
				else
				{
					ImGui::TextDisabled("B 미계측");
					ImGui::TextDisabled("A - B 미계측");
				}
			}
		}

		const char* kind_name(object_kind kind)
		{
			switch (kind)
			{
			case object_kind::model: return "Model";
			case object_kind::material: return "Material";
			case object_kind::texture: return "Texture";
			case object_kind::ui_texture: return "UI Texture";
			case object_kind::sprite_sheet: return "Sprite Sheet";
			}
			return "Object";
		}

		bool contains_case_insensitive(const std::string& value, const char* query)
		{
			if (!query || !*query) return true;
			return std::search(value.begin(), value.end(), query, query + std::strlen(query),
				[](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) ==
					std::tolower(static_cast<unsigned char>(b)); }) != value.end();
		}

		void draw_managed_summary(const snapshot& a, const snapshot* b)
		{
			if (begin_summary_card("##ManagedMemoryCard"))
			{
				ImGui::TextUnformatted("관리 메모리");
				ImGui::Separator();
				if (!a.managed_valid) ImGui::TextDisabled("관리 런타임 값을 수집할 수 없습니다.");
				else
				{
					const std::uint64_t fragmented = (std::min)(a.managed_fragmented_bytes, a.managed_heap_bytes);
					const std::array<bar_part, 2> parts{{
						{ "비단편 힙", a.managed_heap_bytes - fragmented, IM_COL32(108, 174, 183, 255) },
						{ "단편화", fragmented, IM_COL32(231, 199, 85, 255) },
					}};
					segmented_bar("ManagedHeap", parts, a.managed_heap_bytes);
					if (a.managed_heap_bytes == 0) ImGui::TextDisabled("현재 관리 힙이 비어 있습니다.");
					ImGui::TextDisabled("GC 힙 집계입니다. 비단편 힙은 살아 있는 객체 크기와 같지 않습니다.");
				}
				if (a.managed_valid)
					ImGui::TextDisabled("누적 할당 %s (현재 사용량 아님)",
						bytes_label(a.managed_total_allocated_bytes).c_str());
				if (b && a.managed_valid && b->managed_valid)
					ImGui::Text("B 대비 힙 %+.2f MiB",
						(static_cast<double>(a.managed_heap_bytes) - b->managed_heap_bytes) / 1048576.0);
			}
			end_summary_card();
		}

		void draw_category_summary(const snapshot& a)
		{
			if (begin_summary_card("##MemoryCategoryCard"))
			{
				ImGui::TextUnformatted("상위 엔진 객체 범주 · 확인된 CPU 픽셀");
				ImGui::Separator();
				std::array<std::uint64_t, 5> counts{};
				std::array<std::uint64_t, 5> cpuBytes{};
				for (const object_entry& object : a.objects)
				{
					const std::size_t index = static_cast<std::size_t>(object.kind);
					if (index >= counts.size()) continue;
					++counts[index];
					cpuBytes[index] += object.cpu_pixel_bytes;
				}
				const std::array<bar_part, 3> parts{{
					{ "Texture", cpuBytes[static_cast<std::size_t>(object_kind::texture)], IM_COL32(122, 98, 173, 255) },
					{ "UI Texture", cpuBytes[static_cast<std::size_t>(object_kind::ui_texture)], IM_COL32(159, 126, 213, 255) },
					{ "Sprite Sheet", cpuBytes[static_cast<std::size_t>(object_kind::sprite_sheet)], IM_COL32(193, 152, 202, 255) },
				}};
				segmented_bar("EngineCpuPixels", parts, a.texture_cpu_pixel_bytes);
				if (a.texture_cpu_pixel_bytes == 0) ImGui::TextDisabled("보존 중인 CPU 텍스처 픽셀이 없습니다.");
				ImGui::TextDisabled("모델·재질의 정확한 CPU 크기는 아직 계측하지 않습니다.");
				ImGui::Separator();
				if (ImGui::BeginTable("##MemoryObjectCategories", 3,
					ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
				{
					ImGui::TableSetupColumn("범주"); ImGui::TableSetupColumn("객체 수");
					ImGui::TableSetupColumn("CPU 픽셀"); ImGui::TableHeadersRow();
					for (std::size_t i = 0; i < counts.size(); ++i)
					{
						ImGui::TableNextRow();
						ImGui::TableNextColumn(); ImGui::TextUnformatted(kind_name(static_cast<object_kind>(i)));
						ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(counts[i]));
						ImGui::TableNextColumn();
						if (i >= static_cast<std::size_t>(object_kind::texture))
							ImGui::TextUnformatted(bytes_label(cpuBytes[i]).c_str());
						else ImGui::TextDisabled("미계측");
					}
					ImGui::EndTable();
				}
				ImGui::TextDisabled("객체 수는 현행 자산 캐시 기준입니다.");
			}
			end_summary_card();
		}

		void draw_overview(const snapshot& a, const snapshot* b)
		{
			if (begin_summary_card("##MemoryOverviewCard"))
			{
				ImGui::TextUnformatted("메모리 사용 개요");
				ImGui::Separator();
				ImGui::TextDisabled("A: frame %u%s", a.frame, b ? " · 값 아래에 B와 증감 표시" : "");
				struct metric
				{
					const char* name;
					const char* explanation;
					std::uint64_t value;
					bool available;
					ImVec4 color;
					std::uint64_t previous;
					bool previous_available;
					std::uint64_t budget;
				};
				const std::array<metric, 6> metrics{{
					{ "상주 RAM", "OS 작업 집합: 현재 물리 메모리에 상주하는 프로세스 페이지", a.working_set_bytes,
						a.process_valid, { 0.46f, 0.73f, 0.94f, 1.0f }, b ? b->working_set_bytes : 0, b && b->process_valid, 0 },
					{ "Private commit", "OS가 프로세스 전용으로 커밋한 메모리. 상주 RAM과 합산하지 않습니다.", a.private_commit_bytes,
						a.process_valid, { 0.47f, 0.79f, 0.65f, 1.0f }, b ? b->private_commit_bytes : 0, b && b->process_valid, 0 },
					{ "VRAM 표본", "렌더 백엔드의 마지막 어댑터 사용량 표본. 엔진 GPU 객체별 크기는 아닙니다.", a.vram_used_bytes,
						a.vram_valid, { 0.94f, 0.75f, 0.46f, 1.0f }, b ? b->vram_used_bytes : 0, b && b->vram_valid, a.vram_budget_bytes },
					{ "Debug CRT 힙", "Debug CRT의 살아 있는 블록 합계. 개별 객체 소유자는 알 수 없습니다.", a.crt_live_bytes,
						a.crt_heap_valid, { 0.77f, 0.66f, 0.90f, 1.0f }, b ? b->crt_live_bytes : 0, b && b->crt_heap_valid, 0 },
					{ "관리 힙", "CoreCLR의 현재 관리 힙 크기", a.managed_heap_bytes,
						a.managed_valid, { 0.50f, 0.80f, 0.82f, 1.0f }, b ? b->managed_heap_bytes : 0, b && b->managed_valid, 0 },
					{ "CPU 텍스처 픽셀", "자산 캐시에 보관 중인 CPU 픽셀 payload", a.texture_cpu_pixel_bytes,
						true, { 0.87f, 0.68f, 0.62f, 1.0f }, b ? b->texture_cpu_pixel_bytes : 0, b != nullptr, 0 },
				}};
				const int columns = ImGui::GetContentRegionAvail().x >= 720.0f ? 3 : 1;
				if (ImGui::BeginTable("##MemoryOverviewMetrics", columns,
					ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersInnerV |
					ImGuiTableFlags_SizingStretchSame))
				{
					for (std::size_t i = 0; i < metrics.size(); ++i)
					{
						if (i % columns == 0) ImGui::TableNextRow();
						ImGui::TableNextColumn();
						const metric& item = metrics[i];
						metric_tile(item.name, item.explanation, item.value, item.available,
							item.color, b, item.previous, item.previous_available, item.budget);
					}
					ImGui::EndTable();
				}
				ImGui::TextDisabled("OS·CRT·관리 힙·GPU는 범위가 겹칠 수 있어 합산하지 않습니다.");
			}
			end_summary_card();
		}

		void draw_summary(const snapshot& a, const snapshot* b)
		{
			if (begin_summary_card("##MemoryDistributionCard"))
			{
				ImGui::TextUnformatted("프로세스 커밋 주소 영역");
				ImGui::Separator();
				const std::uint64_t total = a.committed_private_bytes + a.committed_image_bytes +
					a.committed_mapped_bytes;
				const std::array<bar_part, 3> parts{{
					{ "Private", a.committed_private_bytes, IM_COL32(104, 189, 159, 255) },
					{ "Image", a.committed_image_bytes, IM_COL32(239, 205, 115, 255) },
					{ "Mapped", a.committed_mapped_bytes, IM_COL32(138, 177, 220, 255) },
				}};
				segmented_bar("CommittedRegions", parts, total);
				ImGui::TextWrapped("VirtualQuery의 커밋 주소 영역입니다. 물리 상주 RAM이나 Private commit과 같은 총량이 아닙니다.");
			}
			end_summary_card();
			summary_card_gap();
			draw_managed_summary(a, b);
			summary_card_gap();
			draw_category_summary(a);
			summary_card_gap();
			draw_overview(a, b);
			summary_card_gap();
			ImGui::TextDisabled("자산 캐시 %zu개 · 주소 영역 %zu개 · 수집 %.2f ms",
				a.objects.size(), a.regions.size(), a.capture_ms);
			if (reader().capture())
			{
				summary_card_gap();
				if (begin_summary_card("##MemoryTrendCard"))
				{
					ImGui::TextUnformatted("프레임별 메모리 추이");
					ImGui::Separator();
					draw_telemetry(telemetry_page::memory);
				}
				end_summary_card();
			}
		}

		void draw_objects(const snapshot& a, const snapshot* b, memory_view_state& view)
		{
			ImGui::TextDisabled("CPU 바이트는 캐시에 보관된 텍스처 픽셀만 정확히 셉니다. 모델 업로드량은 GPU 상주량이 아닙니다.");
			ImGui::InputTextWithHint("##MemoryObjectSearch", "이름 또는 유형 검색", view.search, sizeof(view.search));
			using object_key = std::pair<object_kind, std::string>;
			using compared_object = std::pair<const object_entry*, const object_entry*>;
			std::map<object_key, compared_object> indexed;
			for (const object_entry& object : a.objects)
				indexed[{ object.kind, object.name }].first = &object;
			if (b) for (const object_entry& object : b->objects)
				indexed[{ object.kind, object.name }].second = &object;
			std::vector<compared_object> rows;
			for (const auto& [key, objects] : indexed)
				if (contains_case_insensitive(key.second, view.search) ||
					contains_case_insensitive(kind_name(key.first), view.search)) rows.push_back(objects);
			const auto bytes = [](const object_entry* object)
			{ return object ? object->cpu_pixel_bytes : 0ull; };
			std::sort(rows.begin(), rows.end(), [&](const compared_object& left, const compared_object& right)
			{
				const std::uint64_t leftBytes = bytes(left.first) + bytes(left.second);
				const std::uint64_t rightBytes = bytes(right.first) + bytes(right.second);
				if (leftBytes != rightBytes) return leftBytes > rightBytes;
				const object_entry* leftObject = left.first ? left.first : left.second;
				const object_entry* rightObject = right.first ? right.first : right.second;
				return leftObject->name < rightObject->name;
			});
			ImGui::Text("표시 %zu / A %zu%s", rows.size(), a.objects.size(), b ? " · B 비교 포함" : "");
			if (!ImGui::BeginTable("##MemoryObjects", b ? 6 : 4,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
				ImGuiTableFlags_ScrollY, ImVec2(0, (std::max)(240.0f, ImGui::GetContentRegionAvail().y)))) return;
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("유형", ImGuiTableColumnFlags_WidthFixed, 100.0f);
			ImGui::TableSetupColumn("이름");
			ImGui::TableSetupColumn("CPU 픽셀", ImGuiTableColumnFlags_WidthFixed, 120.0f);
			ImGui::TableSetupColumn("업로드 요청", ImGuiTableColumnFlags_WidthFixed, 155.0f);
			if (b)
			{
				ImGui::TableSetupColumn("B CPU 픽셀", ImGuiTableColumnFlags_WidthFixed, 145.0f);
				ImGui::TableSetupColumn("CPU 증감", ImGuiTableColumnFlags_WidthFixed, 145.0f);
			}
			ImGui::TableHeadersRow();
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(rows.size()));
			while (clipper.Step()) for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
			{
				const object_entry* current = rows[i].first;
				const object_entry* previous = rows[i].second;
				const object_entry& row = *(current ? current : previous);
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::TextUnformatted(kind_name(row.kind));
				ImGui::TableNextColumn(); ImGui::TextUnformatted(row.name.c_str());
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", row.name.c_str());
				ImGui::TableNextColumn();
				if (!current) ImGui::TextDisabled("A에 없음");
				else if (current->cpu_size_known)
				{
					ImGui::TextUnformatted(bytes_label(current->cpu_pixel_bytes).c_str());
					if (current->shared_alias && ImGui::IsItemHovered())
						ImGui::SetTooltip("다른 자산과 공유한 픽셀은 다시 합산하지 않았습니다.");
				}
				else ImGui::TextDisabled("미계측");
				ImGui::TableNextColumn();
				if (current && row.kind == object_kind::model) ImGui::TextUnformatted(bytes_label(current->upload_payload_bytes).c_str());
				else ImGui::TextDisabled("-");
				if (b)
				{
					ImGui::TableNextColumn();
					if (!previous) ImGui::TextDisabled("B에 없음");
					else if (previous->cpu_size_known) ImGui::TextUnformatted(bytes_label(previous->cpu_pixel_bytes).c_str());
					else ImGui::TextDisabled("미계측");
					ImGui::TableNextColumn();
					if ((!current || current->cpu_size_known) && (!previous || previous->cpu_size_known))
						ImGui::Text("%+.2f MiB", (static_cast<double>(current ? current->cpu_pixel_bytes : 0ull) -
							(previous ? previous->cpu_pixel_bytes : 0ull)) / 1048576.0);
					else ImGui::TextDisabled("미계측");
				}
			}
			ImGui::EndTable();
		}

		void draw_all_memory(const snapshot& a, const snapshot* b)
		{
			ImGui::TextWrapped("이 화면은 OS 주소 영역과 엔진이 직접 계측한 부분집합을 나란히 보여줍니다. 중첩되는 관리 힙·텍스처 픽셀을 합산하거나 남는 양을 'Untracked'로 추정하지 않습니다.");
			if (ImGui::BeginTable("##AllMemory", b ? 4 : 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
			{
				ImGui::TableSetupColumn("영역"); ImGui::TableSetupColumn("A");
				if (b) { ImGui::TableSetupColumn("B"); ImGui::TableSetupColumn("A - B"); }
				ImGui::TableHeadersRow();
				metric_row("Private commit 영역", a.committed_private_bytes, true, b, b ? b->committed_private_bytes : 0, b != nullptr);
				metric_row("Image commit 영역", a.committed_image_bytes, true, b, b ? b->committed_image_bytes : 0, b != nullptr);
				metric_row("Mapped commit 영역", a.committed_mapped_bytes, true, b, b ? b->committed_mapped_bytes : 0, b != nullptr);
				metric_row("예약된 가상 주소", a.reserved_virtual_bytes, true, b, b ? b->reserved_virtual_bytes : 0, b != nullptr);
				metric_row("관리 힙 (부분집합)", a.managed_heap_bytes, a.managed_valid, b, b ? b->managed_heap_bytes : 0, b && b->managed_valid);
				metric_row("Debug CRT live heap (부분집합)", a.crt_live_bytes, a.crt_heap_valid, b, b ? b->crt_live_bytes : 0, b && b->crt_heap_valid);
				metric_row("CPU 텍스처 픽셀 (부분집합)", a.texture_cpu_pixel_bytes, true, b, b ? b->texture_cpu_pixel_bytes : 0, b != nullptr);
				metric_row("모델 GPU 업로드 요청량 (할당량 아님)", a.model_upload_payload_bytes, true, b, b ? b->model_upload_payload_bytes : 0, b != nullptr);
				ImGui::EndTable();
			}
			ImGui::Spacing();
			ImGui::TextDisabled("Debug CRT 값은 살아 있는 블록의 합계이며 개별 소유자를 제공하지 않습니다. 관리 객체 참조 그래프와 GPU 개별 리소스 크기는 아직 수집되지 않습니다.");
		}

		const char* region_state(bool committed)
		{
			return committed ? "Commit" : "Reserve";
		}
		const char* region_type(memory_profiler::virtual_region::kind type)
		{
			using kind = memory_profiler::virtual_region::kind;
			return type == kind::image ? "Image" : type == kind::mapped ? "Mapped" :
				type == kind::private_memory ? "Private" : "Unknown";
		}

		void draw_region_tiles(const snapshot& a, memory_view_state& view)
		{
			using region = memory_profiler::virtual_region;
			std::vector<const region*> largest;
			for (const region& entry : a.regions)
				if (entry.committed && entry.bytes > 0) largest.push_back(&entry);
			std::sort(largest.begin(), largest.end(), [](const region* left, const region* right)
			{ return left->bytes > right->bytes; });
			if (largest.size() > 64) largest.resize(64);
			if (largest.empty()) return;
			struct tile { const region* source; ImVec2 top_left; ImVec2 bottom_right; };
			std::vector<tile> tiles;
			tiles.reserve(largest.size());
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const ImVec2 size((std::max)(ImGui::GetContentRegionAvail().x, 100.0f), 170.0f);
			ImGui::InvisibleButton("##MemoryMapTiles", size);
			const auto pack = [&](const auto& self, std::size_t begin, std::size_t end,
				float x, float y, float width, float height) -> void
			{
				if (begin >= end) return;
				if (end - begin == 1)
				{
					tiles.push_back({ largest[begin], { x, y }, { x + width, y + height } });
					return;
				}
				std::uint64_t total = 0;
				for (std::size_t i = begin; i < end; ++i) total += largest[i]->bytes;
				std::uint64_t first = largest[begin]->bytes;
				std::size_t middle = begin + 1;
				for (; middle < end - 1 && first + largest[middle]->bytes <= total / 2; ++middle)
					first += largest[middle]->bytes;
				const float fraction = static_cast<float>(static_cast<double>(first) / total);
				if (width >= height)
				{
					const float leftWidth = width * fraction;
					self(self, begin, middle, x, y, leftWidth, height);
					self(self, middle, end, x + leftWidth, y, width - leftWidth, height);
				}
				else
				{
					const float topHeight = height * fraction;
					self(self, begin, middle, x, y, width, topHeight);
					self(self, middle, end, x, y + topHeight, width, height - topHeight);
				}
			};
			pack(pack, 0, largest.size(), origin.x, origin.y, size.x, size.y);
			const ImVec2 mouse = ImGui::GetMousePos();
			const bool hovered = ImGui::IsItemHovered();
			for (const tile& entry : tiles)
			{
				const bool underMouse = hovered && mouse.x >= entry.top_left.x &&
					mouse.x < entry.bottom_right.x && mouse.y >= entry.top_left.y &&
					mouse.y < entry.bottom_right.y;
				const ImU32 color = entry.source->type == region::kind::image ? IM_COL32(232, 198, 111, 255) :
					entry.source->type == region::kind::mapped ? IM_COL32(111, 164, 219, 255) :
					IM_COL32(104, 185, 148, 255);
				ImGui::GetWindowDrawList()->AddRectFilled(entry.top_left, entry.bottom_right, color);
				ImGui::GetWindowDrawList()->AddRect(entry.top_left, entry.bottom_right,
					underMouse || view.selected_region_address == entry.source->address ?
					IM_COL32(255, 255, 255, 255) : IM_COL32(27, 31, 36, 255), 0.0f, 0, 2.0f);
				if (!underMouse) continue;
				ImGui::SetTooltip("0x%llX · %s · %s", static_cast<unsigned long long>(entry.source->address),
					bytes_label(entry.source->bytes).c_str(), region_type(entry.source->type));
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					view.selected_region_address = entry.source->address;
			}
			ImGui::TextDisabled("큰 커밋 영역 64개 · 면적은 바이트에 비례 · 영역을 클릭하면 아래에 상세가 표시됩니다.");
			if (view.selected_region_address)
			{
				const auto found = std::find_if(a.regions.begin(), a.regions.end(), [&](const region& entry)
				{ return entry.address == view.selected_region_address; });
				if (found != a.regions.end()) ImGui::Text("선택: 0x%llX · %s · %s · 보호 0x%X",
					static_cast<unsigned long long>(found->address), bytes_label(found->bytes).c_str(),
					region_type(found->type), found->protection);
			}
		}

		void draw_map(const snapshot& a, memory_view_state& view)
		{
			ImGui::TextDisabled("VirtualQuery 주소 영역 · 물리 상주량이나 할당 소유자를 뜻하지 않습니다.");
			draw_region_tiles(a, view);
			ImGui::SetNextItemWidth(160.0f);
			ImGui::Combo("영역 필터", &view.region_filter, "전체\0Commit\0Reserve\0\0");
			std::vector<const memory_profiler::virtual_region*> rows;
			for (const auto& region : a.regions)
				if (view.region_filter == 0 ||
					(view.region_filter == 1 && region.committed) ||
					(view.region_filter == 2 && !region.committed)) rows.push_back(&region);
			ImGui::Text("표시 %zu / 전체 %zu 주소 영역", rows.size(), a.regions.size());
			if (!ImGui::BeginTable("##MemoryRegions", 5,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
				ImGuiTableFlags_Resizable, ImVec2(0, (std::max)(240.0f, ImGui::GetContentRegionAvail().y)))) return;
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("시작 주소");
			ImGui::TableSetupColumn("크기");
			ImGui::TableSetupColumn("상태");
			ImGui::TableSetupColumn("유형");
			ImGui::TableSetupColumn("보호");
			ImGui::TableHeadersRow();
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(rows.size()));
			while (clipper.Step()) for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
			{
				const auto& region = *rows[i];
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::Text("0x%llX", static_cast<unsigned long long>(region.address));
				ImGui::TableNextColumn(); ImGui::TextUnformatted(bytes_label(region.bytes).c_str());
				ImGui::TableNextColumn(); ImGui::TextUnformatted(region_state(region.committed));
				ImGui::TableNextColumn(); ImGui::TextUnformatted(region_type(region.type));
				ImGui::TableNextColumn(); ImGui::Text("0x%X", region.protection);
			}
			ImGui::EndTable();
		}
	}

	void draw_memory_profiler()
	{
		memory_view_state& view = state();
		refresh(view);
		auto& service = memory_profiler::snapshot_service::instance();
		ImGui::TextUnformatted("메모리 프로파일러");
		ImGui::SameLine();
		if (service.pending()) ImGui::TextDisabled("스냅샷 수집 중");
		else if (ImGui::Button(EditorIcon::Label<EditorIcon::Camera, " 스냅샷 촬영">)) service.request();
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("다음 GameThread 경계에서 자산·GC를 읽고 프로세스 주소 영역을 수집합니다.");
		ImGui::SameLine();
		ImGui::TextUnformatted("A");
		ImGui::SameLine();
		select_snapshot("##MemorySnapshotA", view, view.selected_a, false);
		ImGui::SameLine();
		ImGui::TextUnformatted("B");
		ImGui::SameLine();
		select_snapshot("##MemorySnapshotB", view, view.selected_b, true);
		const snapshot* a = selected(view, view.selected_a);
		const snapshot* b = selected(view, view.selected_b);
		if (!a)
		{
			ImGui::Separator();
			ImGui::TextDisabled("스냅샷을 촬영하면 요약·객체·전체 메모리·주소 맵과 A/B 비교가 표시됩니다.");
			if (reader().capture()) draw_telemetry(telemetry_page::memory);
			return;
		}
		ImGui::Separator();
		if (ImGui::BeginTabBar("##MemoryProfilerSections"))
		{
			if (ImGui::BeginTabItem("요약")) { draw_summary(*a, b); ImGui::EndTabItem(); }
			if (ImGui::BeginTabItem("엔진 객체")) { draw_objects(*a, b, view); ImGui::EndTabItem(); }
			if (ImGui::BeginTabItem("전체 메모리")) { draw_all_memory(*a, b); ImGui::EndTabItem(); }
			if (ImGui::BeginTabItem("메모리 맵")) { draw_map(*a, view); ImGui::EndTabItem(); }
			ImGui::EndTabBar();
		}
	}
}
