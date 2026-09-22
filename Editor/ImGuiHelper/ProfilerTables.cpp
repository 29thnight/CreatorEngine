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

		// 표를 그리는 **동안만** 칸 여백을 좁힌다.
		//
		// ★ 에디터 테마는 `CellPadding.x = 6` 을 두고 마지막에
		//   `ScaleAllSizes(FontScaleMain × FontScaleDpi)` 를 먹인다. 이 기기에서
		//   1.5 × 1.5 = 2.25 이므로 실제 값이 **13.5** 이고, ImGui 는 열마다
		//   그것을 양쪽에 더한다 — 열 하나가 글자 너비보다 27 px 넓어진다.
		//   열이 열이면 270 px 이 숫자 밖으로 나가고, 그만큼 Marker 열이 좁다.
		//
		// ★ 전역으로 고치지 않는다. 같은 스타일을 Inspector·Content Browser 도
		//   쓰고, 그쪽은 글자 위주라 넉넉한 여백이 맞다. 여기만 좁히고 되돌린다.
		//
		// ★ `BeginTable` **앞에서** 밀어야 한다. 표는 여백을 시작할 때 한 번
		//   읽어 두고 그 뒤로는 스타일을 다시 보지 않는다.
		struct table_padding_scope
		{
			table_padding_scope()
			{
				const ImVec2 padding = ImGui::GetStyle().CellPadding;
				ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
				                    ImVec2(padding.x * 0.5f, padding.y));
			}
			~table_padding_scope() { ImGui::PopStyleVar(); }

			table_padding_scope(const table_padding_scope&) = delete;
			table_padding_scope& operator=(const table_padding_scope&) = delete;
		};

		void time_cell(ce::profile_tick ticks)
		{
			ImGui::Text("%.3f", ticks_to_milliseconds(ticks));
		}

		// 폭을 **주지 않는다.** 폭이 0 인 WidthFixed 열은 ImGui 가 내용과
		// 머리글 중 넓은 쪽에 맞춘다.
		//
		// ★ 여기 80 px 같은 날 숫자가 박혀 있었고, 그래서 머리글이 "Tota..."
		//   로 잘렸다. 테마가 글자에 2.25 배를 먹이는데 이 수만 안 커지기
		//   때문이다.
		//
		// ★ 손으로 `CalcTextSize` 를 재서 넘기는 것도 **틀렸다.** 재 보니 그
		//   값이 실제로 그려지는 너비의 1/1.5 였다 — 테마의 두 배율 중 하나만
		//   반영된다. 화면을 떠서 열 경계를 세기 전까지는 고친 줄 알았다.
		//   ImGui 는 머리글의 이상 너비를 자기가 들고 있으므로, 재지 말고
		//   맡기는 것이 폰트가 바뀌어도 따라간다.
		void auto_column(const char* label)
		{
			ImGui::TableSetupColumn(label, ImGuiTableColumnFlags_WidthFixed);
		}

		void setup_columns()
		{
			ImGui::TableSetupColumn("Marker", ImGuiTableColumnFlags_WidthStretch);
			auto_column("Thread");
			auto_column("Total ms");
			auto_column("Self ms");
			auto_column("Calls");
			auto_column("Avg ms");
			auto_column("Min ms");
			auto_column("Max ms");
			auto_column("P95 ms");
			auto_column("Frames");
			ImGui::TableSetupScrollFreeze(1, 1);
			ImGui::TableHeadersRow();
		}

		// marker 이름 + 잘린 구간 표시. 잘린 줄은 길이가 실제보다 짧으므로
		// 다른 줄과 나란히 두면 안 된다는 것을 이름 옆에 적는다.
		void marker_label(const ce::aggregate_row& row, char* buffer, std::size_t size)
		{
			// ★ 이름은 **캡처에** 묻는다(P6). 전역 registry 로 풀면 파일에서
			//   읽은 캡처가 이 프로세스의 남의 이름을 그린다 — 같은 id 가 남의
			//   빌드에서는 전혀 다른 마커다. 행이 온 캡처와 같은 것에 묻는다.
			const char* name = marker_name(reader().capture(), row.marker);
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
			time_cell(row.min_ticks);
			ImGui::TableNextColumn();
			time_cell(row.max_ticks);
			ImGui::TableNextColumn();
			time_cell(row.p95_ticks);

			// ★ Frames 는 Calls 와 다른 것을 말한다. 한 프레임에 열 번 불린
			//   것과 열 프레임에 한 번씩 불린 것은 Calls 가 같다 — 앞엣것은
			//   그 프레임 하나가 비싼 것이고, 뒤엣것은 늘 켜져 있는 비용이다.
			ImGui::TableNextColumn();
			ImGui::Text("%u", row.frame_appearances);
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

		const table_padding_scope padding;
		if (!ImGui::BeginTable("ProfilerHierarchy", 10, kTableFlags))
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

		const table_padding_scope padding;
		if (!ImGui::BeginTable("ProfilerFlat", 10, kTableFlags))
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

		const table_padding_scope padding;
		if (!ImGui::BeginTable("ProfilerThreads", 4, kTableFlags))
		{
			return;
		}
		ImGui::TableSetupColumn("Thread", ImGuiTableColumnFlags_WidthStretch);
		auto_column("Root ms");
		auto_column("Events");
		auto_column("Depth");
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
