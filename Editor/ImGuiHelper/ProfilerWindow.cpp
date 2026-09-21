// PHASE 14 P3 — 녹화 UI.
//
// 옛 ProfilerWindow(557줄)는 걷었다. 그것은 전역 프로파일러의 vector 를 매
// 프레임 직접 읽어 타임라인을 그렸고, 읽는 동안에도 기록이 계속돼 화면과
// 자료가 어긋났다. 새 코어에서 UI 는 얼린 캡처의 reader 일 뿐이다(§6.4).
//
// ★ 이 층에는 자료를 접는 코드가 없다. Hierarchy/Flat/레인 합계는 전부
//   ProfileAggregate 가 만들고, 선택과 Live Follow 는 ProfileReader 가 든다.
//   그래야 완료조건("Timeline 합계와 Hierarchy inclusive 가 일치", "pause 후
//   엔진이 돌아도 선택 자료가 변하지 않음")을 화면 없이 잰다 — 그리는 것만
//   으로는 살았는지 알 수 없다는 것을 P1·P2 에서 두 번 겪었다.
//
// ★ Space 단축키를 만들지 않는다. §7.1 이 "Space 전역 단축키는 제거하거나
//   Profiler 창 focus 일 때만 받는다" 고 적은 것은 옛 코어 얘기이고, 지금
//   에디터에는 그런 단축키가 없다. 여기서 새로 만들지 않는 것이 그 조건을
//   지키는 가장 싼 방법이다.
#include "ProfilerHUD.h"
#include "ProfilerView.h"

#include <cinttypes>
#include <cstdio>

#include "ImGui.h"
#include "ProfileScope.h"

namespace editor::profiler_view
{
	// 창이 소유하는 reader. 함수 지역 static 이라 창을 닫아도 살아 있다 —
	// 녹화 상태는 서비스가, 선택은 이 reader 가 들고 있으므로 창을 여닫아도
	// 둘 다 유지된다(완료조건).
	ce::capture_reader& reader()
	{
		static ce::capture_reader instance;
		return instance;
	}

	double ticks_to_milliseconds(ce::profile_tick ticks)
	{
		const double frequency =
			static_cast<double>(ce::profiler_service::ticks_per_second());
		if (frequency <= 0.0)
		{
			return 0.0;
		}
		return static_cast<double>(ticks) * 1000.0 / frequency;
	}

	const char* thread_name(const ce::capture_session* capture, std::uint16_t slot)
	{
		if (capture)
		{
			for (const ce::thread_info& info : capture->threads())
			{
				if (info.slot == slot)
				{
					return info.name.c_str();
				}
			}
		}

		// 이름표를 못 찾아도 빈 칸을 내지 않는다. 슬롯 번호만으로도 두 줄이
		// 같은 스레드인지 가릴 수 있다.
		static thread_local char fallback[32];
		std::snprintf(fallback, sizeof(fallback), "slot %u", static_cast<unsigned>(slot));
		return fallback;
	}
}

namespace
{
	const char* state_label(ce::recorder_state state)
	{
		switch (state)
		{
		case ce::recorder_state::recording: return "Recording";
		case ce::recorder_state::frozen:    return "Frozen";
		case ce::recorder_state::pausing:   return "Pausing";
		default:                            return "Stopped";
		}
	}

	void draw_row(const char* label, const char* value)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextUnformatted(label);
		ImGui::TableSetColumnIndex(1);
		ImGui::TextUnformatted(value);
	}

	// 툴바. 녹화 제어와 Live Follow.
	void draw_toolbar(const ce::live_summary& summary)
	{
		using namespace editor::profiler_view;

		ce::profiler_service& service = ce::profiler();
		const bool recording = (summary.state == ce::recorder_state::recording);

		if (ImGui::Button(recording ? "Pause" : "Record"))
		{
			if (recording)
			{
				service.pause();
				// 얼린 그 순간의 것을 곧바로 손에 쥔다. 누른 뒤 한 번 더
				// 무언가를 해야 자료가 나오면 "멈췄는데 빈 화면" 이 된다.
				reader().adopt(service.capture());
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
			reader().reset();
		}

		ImGui::SameLine();
		bool follow = reader().live_follow();
		if (ImGui::Checkbox("Live Follow", &follow))
		{
			reader().set_live_follow(follow);
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("켜면 새로 얼린 캡처에서 최신 프레임을 고른다.\n"
			                  "끄면 보던 프레임을 지킨다 - 스파이크를 붙잡아 둘 때 쓴다.");
		}

		ImGui::SameLine();
		ImGui::Text("%s  ·  frame %u", state_label(summary.state), summary.engine_frame);

		// 녹화 중에는 볼 것이 없다는 것이 설계다(§6.4). 그 사실을 적어 두지
		// 않으면 "타임라인이 안 나온다" 로 읽힌다.
		if (recording)
		{
			ImGui::TextDisabled("녹화 중에는 요약만 공개된다 - Pause 를 눌러야 프레임을 열어 볼 수 있다");
		}

		// ★ 온전하지 않은 캡처를 **말없이** 그리지 않는다. 잠든 워커는 봉인
		//   요청에 응답하지 못해 그 꼬리가 여기 없는데, 아무 말이 없으면
		//   "그 스레드가 조용했다" 로 읽힌다.
		else if (!summary.capture_complete)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
			                   "얼림이 온전하지 않다 - 스트림 %u 의 꼬리가 이 캡처에 없다",
			                   summary.pause_unacked_streams);
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("잠든 스레드는 봉인 요청을 들어줄 자리를 지나지 않는다.\n"
				                  "그 스레드를 깨운 뒤 다시 Pause 하면 꼬리까지 들어온다.");
			}
		}
	}

	void draw_summary(const ce::live_summary& summary)
	{
		if (!ImGui::BeginTable("ProfilerSummary", 2, ImGuiTableFlags_SizingStretchProp))
		{
			return;
		}

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

		// 늦게 온 것의 장부. placed 는 제 프레임 칸으로 돌아간 수, dropped 는
		// 그 칸이 이미 링 밖이라 갈 곳이 없던 수다. stale 은 지운 세대의 것.
		std::snprintf(buffer, sizeof(buffer), "%" PRIu64 " / %" PRIu64,
		              summary.late_events_placed, summary.late_events_dropped);
		draw_row("Late CPU placed / dropped", buffer);

		std::snprintf(buffer, sizeof(buffer), "%" PRIu64, summary.stale_chunks_dropped);
		draw_row("Stale (pre-Clear) dropped", buffer);

		std::snprintf(buffer, sizeof(buffer), "%u", summary.pause_unacked_streams);
		draw_row("Unacked at freeze", buffer);

		// 소유 경계. 셋 다 0 이어야 한다.
		std::snprintf(buffer, sizeof(buffer), "%" PRIu64 " / %" PRIu64,
		              summary.foreign_stream_touches, summary.abandoned_streams);
		draw_row("Foreign touches / abandoned", buffer);

		std::snprintf(buffer, sizeof(buffer), "%" PRIu64, summary.control_requests_deferred);
		draw_row("Control deferred", buffer);

		ImGui::EndTable();
	}

	// 선택 구간의 집계 요약. ★ 두 합이 어긋나면 그 자리에서 드러나야 한다 —
	// 코어 프로브가 잡는 것과 같은 불변식이고, 화면에서도 보이는 편이 낫다.
	void draw_selection_summary()
	{
		using namespace editor::profiler_view;

		const ce::frame_aggregate& aggregate = reader().aggregate();
		ImGui::Text("이벤트 %llu  ·  구간 %.3f ms  ·  레인 %zu",
		            static_cast<unsigned long long>(aggregate.event_count()),
		            ticks_to_milliseconds(aggregate.tick_end() - aggregate.tick_begin()),
		            aggregate.threads().size());

		if (aggregate.timeline_total_ticks() != aggregate.hierarchy_total_ticks())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
			                   "레인 합계와 트리 합계가 어긋난다 (%.3f ms vs %.3f ms) - 집계를 믿지 말 것",
			                   ticks_to_milliseconds(aggregate.timeline_total_ticks()),
			                   ticks_to_milliseconds(aggregate.hierarchy_total_ticks()));
		}
		if (aggregate.truncated_events() > 0)
		{
			ImGui::TextDisabled("잘린 구간 %llu 개 - 그 줄의 길이는 실제보다 짧다",
			                    static_cast<unsigned long long>(aggregate.truncated_events()));
		}
		if (aggregate.dropped_events() > 0)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
			                   "이 구간에서 %llu 개를 잃었다 - 합계를 그대로 믿지 말 것",
			                   static_cast<unsigned long long>(aggregate.dropped_events()));
		}
	}
}

void DrawProfilerHUD()
{
	using namespace editor::profiler_view;

	// ★ 창이 **그려졌다** 는 증거를 프로파일러 자신이 낸다. 창이 열린 것과
	//   본문이 도는 것은 다르다 — 도크 탭으로 겹친 창은 선택돼야 본문이
	//   돌고, 그러지 않으면 `editor.window ... open` 이 성공해도 여기까지
	//   오지 않는다. 이 마커가 캡처에 나타나는지로 게이트가 판정한다.
	//
	//   자기 UI 비용을 자기가 재는 것은 §7 이 말하는 profiler overhead 이기도
	//   하다 — 녹화 중에는 표를 그리지 않으므로(캡처가 없다) 이 구간은 툴바와
	//   요약만 담는다.
	ce::profile_scope _profile{ ce::marker<"ProfilerWindow">() };

	ce::profiler_service& service = ce::profiler();
	const ce::live_summary summary = service.summary();

	// 다른 경로로 얼린 것을 따라간다. CLI 의 profile.pause·profile.frame 둘 다 얼린다.
	//
	// ★ 언제 갈아타는가 는 이 줄이 아니라 reader 가 정한다. 그래야 그 규칙을
	//   화면 없이 재고 변이로 물 수 있다 — 여기 조건문을 두면 재는 수단이 눈뿐이다.
	reader().sync(service.capture());

	draw_toolbar(summary);
	ImGui::Separator();

	if (!ImGui::BeginTabBar("ProfilerTabs"))
	{
		return;
	}

	// ★ Frame Overview 와 Timeline 이 **첫 탭**에 함께 있다. §7.2 가 프레임
	//   그래프를 "모든 분석의 entry point" 라고 부른 대로, 프레임을 고르고
	//   그 자리에서 구간을 들여다보는 것이 한 화면에서 이어져야 한다.
	//
	//   기본 탭이라는 것도 값이다 — 도크 탭은 선택돼야 본문이 돌므로,
	//   뒤 탭에 두면 창을 열어도 타임라인이 한 번도 그려지지 않는다.
	if (ImGui::BeginTabItem("Capture"))
	{
		draw_frame_overview();
		ImGui::Separator();
		if (reader().has_capture())
		{
			draw_selection_summary();
			ImGui::Separator();
			draw_timeline();
		}
		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Hierarchy"))
	{
		if (reader().has_capture())
		{
			draw_selection_summary();
			ImGui::Separator();
			draw_hierarchy_table();
		}
		else
		{
			ImGui::TextDisabled("얼린 캡처가 없다 - Pause 를 누를 것");
		}
		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Flat"))
	{
		if (reader().has_capture())
		{
			draw_selection_summary();
			ImGui::Separator();
			draw_flat_table();
		}
		else
		{
			ImGui::TextDisabled("얼린 캡처가 없다 - Pause 를 누를 것");
		}
		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Threads"))
	{
		if (reader().has_capture())
		{
			draw_thread_table();
		}
		else
		{
			ImGui::TextDisabled("얼린 캡처가 없다 - Pause 를 누를 것");
		}
		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Collector"))
	{
		draw_summary(summary);
		if (summary.dropped_events > 0 || summary.unbalanced_scopes > 0
		    || summary.late_events_dropped > 0 || summary.stale_chunks_dropped > 0
		    || summary.foreign_stream_touches > 0 || summary.abandoned_streams > 0
		    )
		{
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
			                   "수집에 구멍이 있다 - 이 캡처의 합계를 그대로 믿지 말 것");
		}
		if (!summary.capture_complete)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
			                   "얼림 미응답 스트림 %u - 그만큼의 꼬리가 빠져 있다",
			                   summary.pause_unacked_streams);
		}
		ImGui::EndTabItem();
	}

	ImGui::EndTabBar();
}
