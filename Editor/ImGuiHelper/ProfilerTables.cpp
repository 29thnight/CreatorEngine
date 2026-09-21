// PHASE 14 P3 — 집계 표.
//
// Hierarchy 는 부모-자식 호출 트리, Flat 은 이름으로 전부 합친 것이다(§7.4).
// 둘 다 코어가 이미 접어 둔 것을 그리기만 한다 — 이 파일에는 합계 산술이
// 없다. 화면이 숫자를 만들면 그 숫자를 검사할 자리가 화면뿐이 된다.
#include "ProfilerView.h"

#include <cstdio>

#include "ImGui.h"
#include "ProfileMarker.h"

namespace editor::profiler_view
{
	namespace
	{
		constexpr ImGuiTableFlags kTableFlags =
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
			ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;

		void time_cell(ce::profile_tick ticks)
		{
			ImGui::Text("%.3f", ticks_to_milliseconds(ticks));
		}

		void setup_columns()
		{
			ImGui::TableSetupColumn("Marker", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Thread", ImGuiTableColumnFlags_WidthFixed, 130.0f);
			ImGui::TableSetupColumn("Total ms", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupColumn("Self ms", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 60.0f);
			ImGui::TableSetupColumn("Avg ms", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupColumn("Max ms", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupScrollFreeze(1, 1);
			ImGui::TableHeadersRow();
		}

		// marker 이름 + 잘린 구간 표시. 잘린 줄은 길이가 실제보다 짧으므로
		// 다른 줄과 나란히 두면 안 된다는 것을 이름 옆에 적는다.
		void marker_label(const ce::aggregate_row& row, char* buffer, std::size_t size)
		{
			const char* name = ce::marker_info(row.marker).name;
			if (row.truncated)
			{
				std::snprintf(buffer, size, "%s  (잘림)", name);
			}
			else
			{
				std::snprintf(buffer, size, "%s", name);
			}
		}

		void value_cells(const ce::aggregate_row& row)
		{
			const ce::capture_session* capture = reader().capture();

			ImGui::TableNextColumn();
			ImGui::TextUnformatted(thread_name(capture, row.thread_slot));
			ImGui::TableNextColumn();
			time_cell(row.total_ticks);
			ImGui::TableNextColumn();
			time_cell(row.self_ticks);
			ImGui::TableNextColumn();
			ImGui::Text("%llu", static_cast<unsigned long long>(row.call_count));
			ImGui::TableNextColumn();
			time_cell(row.call_count ? (row.total_ticks / row.call_count) : 0);
			ImGui::TableNextColumn();
			time_cell(row.max_ticks);
		}

		// 전위 순서 배열에서 서브트리 하나를 그린다.
		//
		// ★ 루트를 depth 로 판별하지 않는다. 녹화가 도중에 시작되면 부모를
		//   못 본 구간이 depth > 0 인 채로 루트가 된다 — 깊이로 거르면 그런
		//   줄이 표에서 통째로 사라진다.
		void draw_subtree(std::span<const ce::aggregate_row> rows, std::uint32_t index)
		{
			const ce::aggregate_row& row = rows[index];
			const bool hasChildren = row.child_begin < row.child_end;

			ImGui::TableNextRow();
			ImGui::TableNextColumn();

			char label[192];
			marker_label(row, label, sizeof(label));

			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth |
				ImGuiTreeNodeFlags_DefaultOpen;
			if (!hasChildren)
			{
				flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
			}

			ImGui::PushID(static_cast<int>(index));
			const bool open = ImGui::TreeNodeEx(label, flags);
			value_cells(row);

			if (open && hasChildren)
			{
				std::uint32_t child = row.child_begin;
				while (child < row.child_end && child < rows.size())
				{
					draw_subtree(rows, child);
					child = (rows[child].child_end > child) ? rows[child].child_end : (child + 1);
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
	}

	void draw_hierarchy_table()
	{
		const ce::frame_aggregate& aggregate = reader().aggregate();
		const std::span<const ce::aggregate_row> rows = aggregate.hierarchy();
		if (rows.empty())
		{
			ImGui::TextDisabled("선택한 구간에 이벤트가 없다");
			return;
		}

		if (!ImGui::BeginTable("ProfilerHierarchy", 7, kTableFlags))
		{
			return;
		}
		setup_columns();

		// 루트들을 훑는다. 전위 순서에서 한 노드의 서브트리는 연속이므로
		// child_end 로 건너뛰면 다음 루트가 나온다.
		std::uint32_t index = 0;
		while (index < rows.size())
		{
			draw_subtree(rows, index);
			index = (rows[index].child_end > index) ? rows[index].child_end : (index + 1);
		}

		ImGui::EndTable();
	}

	void draw_flat_table()
	{
		const ce::frame_aggregate& aggregate = reader().aggregate();
		const std::span<const ce::aggregate_row> rows = aggregate.flat();
		if (rows.empty())
		{
			ImGui::TextDisabled("선택한 구간에 이벤트가 없다");
			return;
		}

		if (!ImGui::BeginTable("ProfilerFlat", 7, kTableFlags))
		{
			return;
		}
		setup_columns();

		for (const ce::aggregate_row& row : rows)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			char label[192];
			marker_label(row, label, sizeof(label));
			ImGui::TextUnformatted(label);
			value_cells(row);
		}

		ImGui::EndTable();
	}

	void draw_thread_table()
	{
		const ce::frame_aggregate& aggregate = reader().aggregate();
		const std::span<const ce::thread_summary> threads = aggregate.threads();
		if (threads.empty())
		{
			ImGui::TextDisabled("선택한 구간에 이벤트가 없다");
			return;
		}

		if (!ImGui::BeginTable("ProfilerThreads", 4, kTableFlags))
		{
			return;
		}
		ImGui::TableSetupColumn("Thread", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Root ms", ImGuiTableColumnFlags_WidthFixed, 90.0f);
		ImGui::TableSetupColumn("Events", ImGuiTableColumnFlags_WidthFixed, 80.0f);
		ImGui::TableSetupColumn("Depth", ImGuiTableColumnFlags_WidthFixed, 60.0f);
		ImGui::TableHeadersRow();

		const ce::capture_session* capture = reader().capture();
		for (const ce::thread_summary& thread : threads)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(thread_name(capture, thread.thread_slot));
			ImGui::TableNextColumn();
			time_cell(thread.root_ticks);
			ImGui::TableNextColumn();
			ImGui::Text("%u", thread.event_count);
			ImGui::TableNextColumn();
			ImGui::Text("%u", static_cast<unsigned>(thread.max_depth));
		}

		ImGui::EndTable();

		// ★ 워커 구간을 더해 프레임 시간으로 읽지 말라고 적어 둔다. 레인마다
		//   합이 나오면 그것을 더하고 싶어지는데, 워커 구간은 서로 겹치고
		//   소유자의 대기와도 겹친다.
		ImGui::TextDisabled("레인 합계를 더해 프레임 시간으로 읽지 말 것 - 구간이 서로 겹친다");
	}
}
