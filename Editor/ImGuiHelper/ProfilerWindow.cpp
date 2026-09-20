// PHASE 14 P1+P2 — 최소 reader.
//
// 옛 ProfilerWindow(557줄)는 걷었다. 그것은 전역 프로파일러의 vector 를 매
// 프레임 직접 읽어 타임라인을 그렸고, 읽는 동안에도 기록이 계속돼 화면과
// 자료가 어긋났다. 새 코어에서 UI 는 얼린 캡처의 reader 일 뿐이므로
// (계획서 §6.4) 그림을 그대로 옮길 수 없다 — 자료 모델이 다르다.
//
// 본격적인 녹화 UI(Frame Overview · Hierarchy/Flat · 프레임 선택)는 P3 의
// 몫이다. 그때까지 이 자리는 **요약과 녹화 제어만** 낸다. 빈 창을 두지 않는
// 이유는 회귀 때문이다: 계측이 살아 있는지, 스레드가 몇 개 잡히는지, 드롭이
// 있는지를 에디터에서 눈으로 확인할 수단이 사라지면 P3 까지 그 축이 관측
// 밖에 놓인다.
#include "ProfilerHUD.h"

#include <cinttypes>
#include <cstdio>

#include "ImGui.h"
#include "ProfileScope.h"

namespace editor::profiler_hud
{
	inline const char* state_label(ce::recorder_state state)
	{
		switch (state)
		{
		case ce::recorder_state::recording: return "Recording";
		case ce::recorder_state::frozen:    return "Frozen";
		default:                            return "Stopped";
		}
	}

	inline void draw_row(const char* label, const char* value)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextUnformatted(label);
		ImGui::TableSetColumnIndex(1);
		ImGui::TextUnformatted(value);
	}
}

void DrawProfilerHUD()
{
	using namespace editor::profiler_hud;

	ce::profiler_service& service = ce::profiler();
	const ce::live_summary summary = service.summary();

	const bool recording = (summary.state == ce::recorder_state::recording);
	if (ImGui::Button(recording ? "Pause" : "Record"))
	{
		if (recording)
		{
			service.pause();
		}
		else
		{
			service.record(summary.engine_frame);
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear"))
	{
		service.clear();
	}
	ImGui::SameLine();
	ImGui::Text("%s - frame %u", state_label(summary.state), summary.engine_frame);

	ImGui::Separator();

	if (ImGui::BeginTable("ProfilerSummary", 2, ImGuiTableFlags_SizingStretchProp))
	{
		char buffer[128];

		std::snprintf(buffer, sizeof(buffer), "%u", summary.retained_frames);
		draw_row("Retained frames", buffer);

		std::snprintf(buffer, sizeof(buffer), "%u (peak %u)",
		              summary.last_frame_events, summary.peak_frame_events);
		draw_row("Events / frame", buffer);

		std::snprintf(buffer, sizeof(buffer), "%u", summary.thread_count);
		draw_row("Threads", buffer);

		std::snprintf(buffer, sizeof(buffer), "%u", summary.registered_markers);
		draw_row("Markers", buffer);

		std::snprintf(buffer, sizeof(buffer), "%.2f MiB / %.0f MiB",
		              static_cast<double>(summary.memory_bytes) / (1024.0 * 1024.0),
		              static_cast<double>(summary.memory_budget) / (1024.0 * 1024.0));
		draw_row("Capture memory", buffer);

		std::snprintf(buffer, sizeof(buffer), "%u / %u", summary.free_chunks, summary.chunk_count);
		draw_row("Free chunks", buffer);

		// 잃은 것과 어긋난 것은 0 이 아니면 눈에 띄어야 한다. 프로파일러가
		// 스스로 잃은 수를 감추면 그 수치를 근거로 내리는 판단이 전부 틀어진다.
		std::snprintf(buffer, sizeof(buffer), "%" PRIu64, summary.dropped_events);
		draw_row("Dropped events", buffer);

		std::snprintf(buffer, sizeof(buffer), "%" PRIu64, summary.unbalanced_scopes);
		draw_row("Unbalanced scopes", buffer);

		ImGui::EndTable();
	}

	if (summary.dropped_events > 0 || summary.unbalanced_scopes > 0)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
		                   "수집에 구멍이 있다 - 이 캡처의 합계를 그대로 믿지 말 것");
	}

	ImGui::Separator();
	ImGui::TextDisabled("Timeline/Hierarchy: PHASE 14 P3");
}
