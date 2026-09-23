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
#include <memory>
#include <string>

#include "ImGui.h"
#include "ProfileCaptureFile.h"
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
		// ★ 환산은 **캡처가 뜬 기계의** 주파수로 한다(P6). 이 기계의 QPC 로
		//   나누면 남의 기계에서 뜬 .ceprof 의 모든 구간 길이가 두 주파수의
		//   비만큼 틀리고, 화면에는 그럴듯한 숫자가 그대로 나온다.
		//
		//   캡처가 없으면 환산할 것도 없다. 이 기계의 주파수로 물러나지
		//   **않는다** — 물러나는 순간 그 경로가 파일 캡처에서도 돌 수 있다.
		const ce::capture_session* capture = reader().capture();
		const double frequency = capture
			? static_cast<double>(capture->environment().ticks_per_second)
			: 0.0;
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

	const char* marker_name(const ce::capture_session* capture, ce::marker_id id)
	{
		if (capture)
		{
			const ce::capture_marker& info = capture->marker(id);
			if (!info.name.empty())
			{
				return info.name.c_str();
			}
		}

		// 표에 없는 id. 전역 registry 로 물러나지 **않는다** — 그러면 남의
		// 빌드에서 온 캡처가 이 프로세스의 엉뚱한 이름을 그린다. 대신 id 를
		// 그대로 보여 준다: 모른다는 것이 화면에 보여야 한다.
		static thread_local char fallback[32];
		std::snprintf(fallback, sizeof(fallback), "marker %u", static_cast<unsigned>(id));
		return fallback;
	}
}

// ★ 이름 있는 네임스페이스에 둔다. 아래의 익명 네임스페이스는 유니티 blob 에서
//   옆 파일과 **공유**되므로, `opened()` 같은 흔한 이름이 남의 것과 겹친다.
namespace editor::profiler_view::capture_file_view
{
	// ── 파일(P6-3) ────────────────────────────────────────────────────────
	//
	// 파일에서 연 캡처가 **무엇인가** — 표시용이다. 라이브에 덮이지 않게 하는
	// 정책은 코어의 capture_reader::open() 이 진다(재기 위해서).
	//
	// ★ weak_ptr 로 든다. reader 가 그 캡처를 놓으면(따라가기를 켜 라이브로
	//   돌아가면) lock() 이 비어 "파일: …" 표시가 **저절로** 사라진다. 원시
	//   포인터로 비교하면 풀린 주소에 새 라이브 캡처가 앉을 때 파일로 오인한다.
	struct opened_capture
	{
		std::weak_ptr<const ce::capture_session> capture;
		std::string                              name;
	};

	opened_capture& opened()
	{
		static opened_capture value;
		return value;
	}

	// 마지막 저장·열기의 결과. 실패를 **말없이** 삼키지 않는다 — 손상된 파일을
	// 열었는데 아무 일도 안 일어나면 사용자는 버튼이 안 먹는다고 읽는다.
	std::string& file_message()
	{
		static std::string value;
		return value;
	}

	std::string utf8_file_name(const std::filesystem::path& path)
	{
		const std::u8string name = path.filename().u8string();
		return std::string(name.begin(), name.end());
	}

	void save_viewed_capture()
	{
		const ce::capture_session* capture = reader().capture();
		if (!capture)
		{
			return;
		}
		const std::filesystem::path path = pick_capture_to_save();
		if (path.empty())
		{
			return;   // 취소는 결과가 아니다
		}

		// ★ 보고 있는 캡처를 쓴다. 얼린 캡처는 아무도 고치지 않으므로 저장하는
		//   동안 녹화가 이어져도 쓰는 것이 변하지 않는다(§P6 완료 조건 넷째).
		const auto saved = ce::save_capture(*capture, path);
		file_message() = saved
			? "저장했다 - " + utf8_file_name(path)
			: std::string("저장하지 못했다 - ") + ce::describe(saved.error());
	}

	void open_capture_file()
	{
		const std::filesystem::path path = pick_capture_to_open();
		if (path.empty())
		{
			return;
		}
		const auto loaded = ce::load_capture(path);
		if (!loaded)
		{
			file_message() = std::string("열지 못했다 - ") + ce::describe(loaded.error());
			return;
		}

		reader().open(*loaded);
		opened() = opened_capture{ *loaded, utf8_file_name(path) };
		file_message().clear();
	}

	void draw_file_line()
	{
		const ce::capture_session* shown = reader().capture();
		const std::shared_ptr<const ce::capture_session> file = opened().capture.lock();
		if (shown && file.get() == shown && shown->frame_count() > 0)
		{
			ImGui::TextColored(ImVec4(0.55f, 0.80f, 1.0f, 1.0f),
			                   "파일: %s  ·  frame %u..%u (%u)  ·  이벤트 %" PRIu64 "  ·  스레드 %zu",
			                   opened().name.c_str(),
			                   shown->frames().front().engine_frame,
			                   shown->frames().back().engine_frame,
			                   shown->frame_count(),
			                   shown->total_events(),
			                   shown->threads().size());
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("파일에서 연 캡처를 보고 있다 - 녹화가 돌아도 이 화면은 안 바뀐다.\n"
				                  "라이브로 돌아가려면 Live Follow 를 켠다.");
			}
		}
		if (!file_message().empty())
		{
			ImGui::TextDisabled("%s", file_message().c_str());
		}
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

		// ★ 토글이다. Unity 의 ⏺ 와 같은 뜻 — 켜면 모으고 끄면 모으지 않는다.
		//   "Pause" 라고 적던 시절에는 **보려면 멈춰야 했으므로** 그 이름이
		//   맞았다. 보는 것과 모으는 것이 갈렸으니 이름도 갈린다.
		bool record = recording;
		if (ImGui::Checkbox("Record", &record))
		{
			if (recording)
			{
				// ★ 부르고 **기다리지 않는다.** 이 자리는 씬 잠금을 쥔 UI
				//   스레드이고, 수집기는 같은 잠금을 통과해야 이 요청을
				//   처리한다 — 여기서 기다리면 서로를 기다린다(§0.5.18).
				//
				//   그래서 여기서 capture() 를 쥐면 **직전 것**이거나 비어
				//   있다. 얼림이 끝나는 프레임에 아래의 sync() 가 받는다.
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
			reader().reset();
		}

		ImGui::SameLine();
		ImGui::BeginDisabled(!reader().has_capture());
		if (ImGui::Button("Save"))
		{
			editor::profiler_view::capture_file_view::save_viewed_capture();
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("Open"))
		{
			editor::profiler_view::capture_file_view::open_capture_file();
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

		// ★ 녹화 중 화면은 **한 박자 뒤처진다.** 코어가 정한 간격으로만
		//   스냅샷을 내고, 늦게 오는 GPU 구간은 닫힌 프레임에 나중에 들어간다
		//   (실측 제출→수집 최대 94 ms). 그 사실을 적어 두지 않으면 "최신
		//   프레임에 GPU 막대가 없다" 를 결함으로 읽는다.
		if (recording)
		{
			ImGui::TextDisabled("녹화 중 - 화면은 마지막 스냅샷이다 (GPU 구간은 몇 프레임 뒤에 채워진다)");
		}

		// ★ 온전하지 않은 캡처를 **말없이** 그리지 않는다. 잠든 워커는 봉인
		//   요청에 응답하지 못해 그 꼬리가 여기 없는데, 아무 말이 없으면
		//   "그 스레드가 조용했다" 로 읽힌다.
		//
		// ★ **보고 있는 캡처**의 표식을 읽는다. 라이브 서비스의 요약을 읽으면
		//   파일을 보는 동안 남의 캡처 이야기를 한다 — 캡처가 제 온전함을 들고
		//   다니는 이유가 그것이다(ProfileCapture.h).
		else if (const ce::capture_session* shown = reader().capture();
		         shown && !shown->complete())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
			                   "얼림이 온전하지 않다 - 스트림 %u 의 꼬리가 이 캡처에 없다",
			                   shown->unacked_streams());
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

	// ★ 녹화 중에도 프레임을 보여 준다(§6.4 개정). 창이 떠 있는 동안만
	//   청하므로, 창을 닫으면 코어는 스냅샷을 한 번도 만들지 않는다.
	//   간격은 코어가 정한다 — 화면이 부르는 대로 다 내주면 링을 통째로
	//   복사하는 비용이 재려는 대상을 흔든다.
	service.request_live_capture();

	const ce::live_summary summary = service.summary();

	// 다른 경로로 얼린 것을 따라간다. CLI 의 profile.pause·profile.frame 둘 다 얼린다.
	//
	// ★ 언제 갈아타는가 는 이 줄이 아니라 reader 가 정한다. 그래야 그 규칙을
	//   화면 없이 재고 변이로 물 수 있다 — 여기 조건문을 두면 재는 수단이 눈뿐이다.
	reader().sync(service.capture());

	draw_toolbar(summary);
	editor::profiler_view::capture_file_view::draw_file_line();
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
			ImGui::TextDisabled("아직 캡처가 없다 - Record 를 켤 것");
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
			ImGui::TextDisabled("아직 캡처가 없다 - Record 를 켤 것");
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
			ImGui::TextDisabled("아직 캡처가 없다 - Record 를 켤 것");
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
