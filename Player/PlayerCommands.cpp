#include "PlayerCommands.h"

#include <string_view>

namespace
{
    bool IsLocalCommandArgument(std::wstring_view option)
    {
        return option == L"--exec" || option == L"--exec-args" || option == L"--script" ||
            option == L"--commandlet" || option == L"--commandlet-script" ||
            option == L"--result-format" || option == L"--result-file" || option == L"--fail-fast";
    }
}

#if CE_DEVELOPMENT
#include "PlayerCommandService.h"
#include "CommandCore/TemporalCommands.h"
#include "CommandCore/CommandParser.h"
#include "CommandCore/CommandSession.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "CommandCore/CommandDescriptorSeeds.h"
#include "CommandCore/CommandRegistry.h"

#include "Entity.h"
#include "Animator.h"
#include "MeshRenderer.h"
#include "Scene.h"
#include "SceneManager.h"
#include "TimeSystem.h"
#include "Transform.h"

#if !CE_SHIPPING
#include "ProfileService.h"
#include "ProfileCaptureFile.h"
#include "JobScheduler.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RuntimeSettings.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <exception>
#include <memory>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace PlayerCmd
{
	namespace
	{
        struct BatchInput
        {
            std::vector<std::vector<std::string>> commands;
            std::filesystem::path resultPath;
            std::ofstream resultFile;
            std::size_t next{};
            bool requested{};
            bool commandlet{};
            bool modifiers{};
            bool failFast{};
            bool started{};
            bool awaiting{};
            bool completed{};
        };

        BatchInput g_batch;

        std::string Utf8(const wchar_t* value)
        {
            const std::wstring_view text(value);
            const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            if (length == 0 && !text.empty())
            {
                throw std::invalid_argument("Command argument is not valid Unicode");
            }
            std::string result(static_cast<std::size_t>(length), '\0');
            if (length > 0)
            {
                WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                    static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
            }
            return result;
        }

        bool AddBatchLine(std::string_view line, std::string& error)
        {
            const auto first = line.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos || line[first] == '#' || line.substr(first, 2) == "//")
            {
                return true;
            }
            auto parsed = CommandCore::Tokenize(line);
            if (!parsed.ok)
            {
                error = parsed.errorCode + ": " + parsed.errorMessage;
                return false;
            }
            if (!parsed.tokens.empty())
            {
                g_batch.commands.push_back(std::move(parsed.tokens));
            }
            return true;
        }

        bool LoadBatchScript(const std::filesystem::path& path, std::string& error)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                error = "Cannot open Player command script";
                return false;
            }
            std::string line;
            std::size_t lineNumber = 0;
            while (std::getline(input, line))
            {
                ++lineNumber;
                if (lineNumber == 1 && line.starts_with("\xEF\xBB\xBF"))
                {
                    line.erase(0, 3);
                }
                if (!AddBatchLine(line, error))
                {
                    error = "Script line " + std::to_string(lineNumber) + ": " + error;
                    return false;
                }
            }
            if (input.bad())
            {
                error = "Cannot read Player command script";
                return false;
            }
            return true;
        }

        void FinishBatch()
        {
            if (g_batch.completed)
            {
                return;
            }
            g_batch.completed = true;
            std::fprintf(stderr, "[PLAYER CLI] %s\n", CommandCore::CommandSession::Batch().Summary().c_str());
            if (g_batch.commandlet || CommandCore::CommandSession::Batch().ShouldStopEarly())
            {
                CommandHost::Get().RequestQuit();
            }
        }

        void QueueNextBatchCommand()
        {
            if (!g_batch.started || g_batch.awaiting || g_batch.completed)
            {
                return;
            }
            if (g_batch.next == g_batch.commands.size())
            {
                FinishBatch();
                return;
            }
            auto arguments = std::move(g_batch.commands[g_batch.next++]);
            const std::string command = arguments[0];
            g_batch.awaiting = true;
            CommandHost::Get().Enqueue(std::move(arguments),
                [command](const CommandCore::CommandResult& result, const Timing& timing)
                {
                    auto& session = CommandCore::CommandSession::Batch();
                    session.Record(command, result);
                    const std::string json = PlayerCommandService::FormatResult(command, result, timing);
                    std::fprintf(stdout, "%s\n", json.c_str());
                    std::fflush(stdout);
                    if (g_batch.resultFile.is_open())
                    {
                        g_batch.resultFile << json << '\n';
                        g_batch.resultFile.flush();
                        if (!g_batch.resultFile)
                        {
                            session.Record("--result-file", CommandCore::InternalError(
                                "result.write_failed", "Cannot write Player result file"));
                            CommandHost::Get().RequestQuit();
                        }
                    }
                    g_batch.awaiting = false;
                    if (g_batch.next == g_batch.commands.size() || session.ShouldStopEarly() ||
                        CommandHost::Get().IsQuitRequested())
                    {
                        FinishBatch();
                    }
                }, 0);
        }

		using Handler = CommandCore::CommandResult(*)(const std::vector<std::string>&);

		// ── 핸들러 ──────────────────────────────────────────────────────
		//
		// 전부 `static` 이다. Editor 의 도메인 TU 와 같은 규약 — 핸들러 주소가
		// TU 밖으로 나갈 일이 없으므로 외부 링크 심볼을 늘리지 않는다.

		Scene* ActiveScene()
		{
			return SceneManagers->GetActiveScene();
		}

		CommandCore::CommandData Vector3Data(const math::vector3& value)
		{
			CommandCore::CommandData object = CommandCore::CommandData::Object();
			object.Set("x", CommandCore::CommandData::Double(value.x));
			object.Set("y", CommandCore::CommandData::Double(value.y));
			object.Set("z", CommandCore::CommandData::Double(value.z));
			return object;
		}

		CommandCore::CommandResult Cmd_help(const std::vector<std::string>& parts)
		{
			const CommandCore::CommandRegistry& registry = CommandCore::CommandRegistry::Get();

			if (parts.size() > 1)
			{
				const CommandCore::CommandDescriptor* descriptor = registry.Find(parts[1]);
				if (nullptr == descriptor)
				{
					// ★ Editor 에 있는 이름이라도 여기서는 "알 수 없는 명령" 이다.
					//   Player registry 에 **부재**하기 때문이고, 그것이 §11.2 가
					//   말하는 "런타임 거부가 아니라 부재" 다.
					return CommandCore::InvalidArguments(
						"알 수 없는 명령: " + parts[1], "command.unknown");
				}
				std::fputs(CommandCore::RenderCommandDetail(*descriptor).c_str(), stdout);
				return CommandCore::Ok();
			}

			std::fputs(CommandCore::RenderHelp(registry).c_str(), stdout);
			return CommandCore::Ok();
		}

		CommandCore::CommandResult Cmd_quit(const std::vector<std::string>&)
		{
			CommandHost::Get().RequestQuit();
			return CommandCore::Ok("종료 요청");
		}

		CommandCore::CommandResult Cmd_status(const std::vector<std::string>&)
		{
			const CommandHost::Status status = CommandHost::Get().Snapshot();

			CommandCore::CommandData data = CommandCore::CommandData::Object();
			data.Set("frame", CommandCore::CommandData::Int(
				static_cast<int64_t>(status.frame)));
			data.Set("gameStart", CommandCore::CommandData::Bool(
				SceneManagers->IsGameStart()));
			data.Set("queueDepth", CommandCore::CommandData::Int(
				static_cast<int64_t>(status.queueDepth)));
			return CommandCore::Ok("player status", std::move(data));
		}

		CommandCore::CommandResult Cmd_scene(const std::vector<std::string>&)
		{
			Scene* scene = ActiveScene();
			if (nullptr == scene)
			{
				return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
			}

			CommandCore::CommandData data = CommandCore::CommandData::Object();
			data.Set("name", CommandCore::CommandData::String(
				scene->GetSceneName().ToString()));
			data.Set("objects", CommandCore::CommandData::Int(
				static_cast<int64_t>(scene->m_Entities.size())));
            data.Set("simulating", CommandCore::CommandData::Bool(SceneManagers->IsGameStart()));
            data.Set("editorSceneLoaded", CommandCore::CommandData::Bool(SceneManagers->IsEditorSceneLoaded()));
            data.Set("hasAuthoringSnapshot", CommandCore::CommandData::Bool(SceneManagers->HasSceneSnapshot()));
			return CommandCore::Ok("scene", std::move(data));
		}

		CommandCore::CommandResult Cmd_objects(const std::vector<std::string>& parts)
		{
			Scene* scene = ActiveScene();
			if (nullptr == scene)
			{
				return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
			}

			const std::string filter = (parts.size() > 1) ? parts[1] : std::string();

			CommandCore::CommandData names = CommandCore::CommandData::Array();
			for (const auto& entity : scene->m_Entities)
			{
				if (!entity) continue;

				const std::string name = entity->GetHashedName().ToString();
				if (!filter.empty() && name.find(filter) == std::string::npos) continue;

				names.Append(CommandCore::CommandData::String(name));
			}

			CommandCore::CommandData data = CommandCore::CommandData::Object();
			data.Set("count", CommandCore::CommandData::Int(
				static_cast<int64_t>(names.Items().size())));
			data.Set("names", std::move(names));
			return CommandCore::Ok("objects", std::move(data));
		}

		CommandCore::CommandResult Cmd_object(const std::vector<std::string>& parts)
		{
			if (parts.size() < 2)
			{
				return CommandCore::InvalidArguments("player.object: <이름> 이 필요하다");
			}

			Scene* scene = ActiveScene();
			if (nullptr == scene)
			{
				return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
			}

			Entity* object = scene->GetEntity(parts[1]);
			if (nullptr == object)
			{
				return CommandCore::Fail("object.not_found",
					"오브젝트를 찾을 수 없다: " + parts[1]);
			}

			Transform& transform = object->Transform_();
			const math::quaternion rotation = transform.GetRotation();

			CommandCore::CommandData rotationData = CommandCore::CommandData::Object();
			rotationData.Set("x", CommandCore::CommandData::Double(rotation.x));
			rotationData.Set("y", CommandCore::CommandData::Double(rotation.y));
			rotationData.Set("z", CommandCore::CommandData::Double(rotation.z));
			rotationData.Set("w", CommandCore::CommandData::Double(rotation.w));

			CommandCore::CommandData data = CommandCore::CommandData::Object();
			data.Set("name", CommandCore::CommandData::String(parts[1]));
			data.Set("position", Vector3Data(transform.GetPosition()));
			data.Set("worldPosition", Vector3Data(transform.GetWorldPosition()));
			data.Set("rotation", std::move(rotationData));
			data.Set("scale", Vector3Data(transform.GetScale()));
			return CommandCore::Ok("object", std::move(data));
		}

		CommandCore::CommandResult Cmd_animation(const std::vector<std::string>& parts)
		{
			if (parts.size() != 2)
				return CommandCore::InvalidArguments("player.animation: <이름> 이 필요하다");
			Scene* scene = ActiveScene();
			if (!scene)
				return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
			Entity* object = scene->GetEntity(parts[1]);
			Animator* animator = object ? object->GetComponent<Animator>() : nullptr;
			if (!animator)
				return CommandCore::Fail("animation.not_found", "Animator를 찾을 수 없다: " + parts[1]);

			// Commands run on the game thread before simulation. The previous
// AnimationScheduler::Update has already joined its workers at this point.
			const AnimInstance& instance = animator->GetInstance();
			std::uint64_t digest = 14695981039346656037ull;
			for (const auto& matrix : instance.finalTransforms)
			{
				std::array<float, 16> values{};
				std::memcpy(values.data(), &matrix, sizeof(values));
				for (float value : values)
				{
					digest ^= std::bit_cast<std::uint32_t>(value);
					digest *= 1099511628211ull;
				}
			}
			std::ostringstream digestText;
			digestText << std::hex << std::setw(16) << std::setfill('0') << digest;
			std::size_t skinnedMeshes = 0;
			for (const MeshRenderer* mesh : object->GetComponentsInChildren<MeshRenderer>())
				if (mesh->IsSkinnedMesh()) ++skinnedMeshes;

			CommandCore::CommandData data = CommandCore::CommandData::Object();
			data.Set("name", CommandCore::CommandData::String(parts[1]));
			data.Set("clip", CommandCore::CommandData::String(
				animator->GetClipName(static_cast<int>(instance.selectedClipIndex))));
			data.Set("time", CommandCore::CommandData::Double(instance.timeElapsed));
			data.Set("bones", CommandCore::CommandData::Int(
				static_cast<int64_t>(instance.finalTransforms.size())));
			data.Set("skinnedMeshes", CommandCore::CommandData::Int(
				static_cast<int64_t>(skinnedMeshes)));
			data.Set("paletteDigest", CommandCore::CommandData::String(digestText.str()));
			return CommandCore::Ok("animation", std::move(data));
		}

		CommandCore::CommandResult Cmd_move(const std::vector<std::string>& parts)
		{
			if (parts.size() < 5)
			{
				return CommandCore::InvalidArguments(
					"player.move: <이름> <x> <y> <z> 가 필요하다");
			}

			Scene* scene = ActiveScene();
			if (nullptr == scene)
			{
				return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
			}

			Entity* object = scene->GetEntity(parts[1]);
			if (nullptr == object)
			{
				return CommandCore::Fail("object.not_found",
					"오브젝트를 찾을 수 없다: " + parts[1]);
			}

			const math::vector3 position{
				static_cast<float>(std::atof(parts[2].c_str())),
				static_cast<float>(std::atof(parts[3].c_str())),
				static_cast<float>(std::atof(parts[4].c_str())) };

			Transform& transform = object->Transform_();
			transform.SetPosition(position);
			transform.UpdateWorldMatrix();

			// ★ Editor 의 `object.transform` 이 여기서 하는 일 하나를 **하지 않는다** —
			//   `PrefabUtility::RecordPropertyOverride`. Player 에는 저작이 없고
			//   (`enableAssetAuthoring = false`), 프리팹 오버라이드는 디스크에 남길
			//   저작 기록이다. 런타임에서 오브젝트를 옮긴 것을 저작 의도로 기록하면
			//   실행 중 게임 상태가 프로젝트 자산으로 새어 나간다.
			CommandCore::CommandData data = CommandCore::CommandData::Object();
			data.Set("name", CommandCore::CommandData::String(parts[1]));
			data.Set("position", Vector3Data(transform.GetPosition()));
			return CommandCore::Ok("이동 완료: " + parts[1], std::move(data));
		}

#if !CE_SHIPPING
		// 시험 전용: 다음 프레임 시작에 DX12 장치를 지워 장치 제거 경로를 재현한다.
		// scene 은 렌더 장치, host 는 표시 장치지만 같은 어댑터라 실제로는 둘 다 지워진다.
		CommandCore::CommandResult Cmd_device_remove(const std::vector<std::string>& parts)
		{
			if (parts.size() != 2 || (parts[1] != "scene" && parts[1] != "host"))
			{
				return CommandCore::InvalidArguments("player.device-remove: scene|host 가 필요하다");
			}
			if (RenderBackend::DX12 != RuntimeSettings::Get().GetRenderBackend())
			{
				return CommandCore::Fail("device.backend",
					"player.device-remove: DX12 백엔드에서만 주입할 수 있다");
			}
			DX12DeviceResources::RequestTestDeviceRemoval(parts[1] == "scene"
				? DX12DeviceResources::TestDeviceRemovalTarget::Scene
				: DX12DeviceResources::TestDeviceRemovalTarget::Host);
			auto data = CommandCore::CommandData::Object();
			data.Set("target", CommandCore::CommandData::String(parts[1]));
			return CommandCore::Ok("장치 제거 요청: " + parts[1], std::move(data));
		}
#endif

#if !CE_SHIPPING
        struct ProfileSaveOperation
        {
            std::atomic_bool done{ false };
            std::string path;
            std::string error = "Save was not accepted by the worker scheduler";
            std::uint64_t frames = 0;
            bool complete = false;
            std::uint32_t unacked = 0;
        };

        // 실행되지 않은 콜백의 마지막 소유자가 실패를 공개한다. 호출자는 기다리지 않는다.
        struct ProfileSaveAdmission
        {
            explicit ProfileSaveAdmission(std::shared_ptr<ProfileSaveOperation> value) : operation(std::move(value)) {}

            ~ProfileSaveAdmission()
            {
                if (!started.load(std::memory_order_acquire))
                {
                    operation->done.store(true, std::memory_order_release);
                }
            }

            std::shared_ptr<ProfileSaveOperation> operation;
            std::atomic_bool started{ false };
        };

        static std::shared_ptr<ProfileSaveOperation>& ProfileSaveState()
        {
            static std::shared_ptr<ProfileSaveOperation> operation;
            return operation;
        }

        static void AddRecordingPayload(CommandCore::CommandData& data)
        {
            using namespace CommandCore;
            const ce::recording_status status = ce::profiler().recording_status();
            const auto path = ce::profiler().recording_path().u8string();
            auto recording = CommandData::Object();
            recording.Set("path", CommandData::String(std::string(path.begin(), path.end())));
            recording.Set("state", CommandData::String(status.error ? "failed" :
                ce::profiler().state() == ce::recorder_state::starting ? "starting" : path.empty() ? "none" :
                status.state == ce::recording_state::starting ? "starting" :
                status.state == ce::recording_state::recording ? "recording" :
                status.state == ce::recording_state::flushing ? "flushing" :
                status.state == ce::recording_state::finalized ? "finalized" : "failed"));
            recording.Set("queuedBytes", CommandData::Int(status.queued_bytes));
            recording.Set("queuedBatches", CommandData::Int(status.queued_batches));
            recording.Set("writtenBytes", CommandData::Int(status.written_bytes));
            recording.Set("flushedBytes", CommandData::Int(status.flushed_bytes));
            recording.Set("writtenFrames", CommandData::Int(status.written_frames));
            recording.Set("submittedFrames", CommandData::Int(status.submitted_frames));
            recording.Set("droppedFrames", CommandData::Int(status.dropped_frames));
            recording.Set("droppedEvents", CommandData::Int(status.dropped_events));
            recording.Set("droppedCounters", CommandData::Int(status.dropped_counters));
            recording.Set("sourceDroppedCounters", CommandData::Int(status.source_dropped_counters));
            recording.Set("sourceDroppedEvents", CommandData::Int(status.source_losses.dropped_events));
            recording.Set("sourceDroppedFrameBoundaries", CommandData::Int(status.source_losses.dropped_frame_boundaries));
            recording.Set("lateCpuEvents", CommandData::Int(status.source_losses.late_events));
            recording.Set("lateGpuSpans", CommandData::Int(status.source_losses.late_gpu_spans));
            recording.Set("error", CommandData::String(status.error ? ce::describe(*status.error) : ""));
            data.Set("writer", std::move(recording));
        }

        static CommandCore::CommandData ProfileSavePayload()
        {
            using namespace CommandCore;
            auto data = CommandData::Object();
            AddRecordingPayload(data);
            const auto operation = ProfileSaveState();
            const bool done = operation && operation->done.load(std::memory_order_acquire);
            data.Set("saveState", CommandData::String(!operation ? "idle" : !done ? "saving" :
                operation->error.empty() ? "saved" : "failed"));
            data.Set("pending", CommandData::Bool(operation && !done));
            data.Set("path", CommandData::String(operation ? operation->path : ""));
            if (done)
            {
                data.Set("frames", CommandData::Int(operation->frames));
                data.Set("complete", CommandData::Bool(operation->complete));
                data.Set("unacked", CommandData::Int(operation->unacked));
                data.Set("error", CommandData::String(operation->error));
            }
            return data;
        }

        static CommandCore::CommandData ProfileStatePayload()
        {
            const ce::live_summary summary = ce::profiler().summary();
            auto data = CommandCore::CommandData::Object();
            data.Set("recording", CommandCore::CommandData::Bool(summary.state == ce::recorder_state::recording));
            data.Set("state", CommandCore::CommandData::String(
                summary.state == ce::recorder_state::starting ? "starting" :
                summary.state == ce::recorder_state::recording ? "recording" :
                summary.state == ce::recorder_state::pausing ? "pausing" :
                summary.state == ce::recorder_state::frozen ? "frozen" : "stopped"));
            data.Set("engineFrame", CommandCore::CommandData::Int(summary.engine_frame));
            const ce::capture_session_ptr capture = ce::profiler().capture();
            data.Set("hasCapture", CommandCore::CommandData::Bool(bool(capture)));

            // ★ "얼렸다" 와 "온전하게 얼렸다" 는 다르다. 잠든 워커는 봉인 요청에
            //   응답하지 못하므로 그 꼬리가 캡처에 없는데, 이것을 내지 않으면
            //   자동화는 빈 레인을 "그 스레드가 조용했다" 로 읽는다.
            data.Set("captureComplete",
                     CommandCore::CommandData::Bool(capture ? capture->complete() : true));
            data.Set("unackedStreams", CommandCore::CommandData::Int(
                capture ? static_cast<std::int64_t>(capture->unacked_streams()) : 0));
            AddRecordingPayload(data);
            return data;
        }

        static CommandCore::CommandResult Cmd_profile_record(const std::vector<std::string>& parts)
        {
            using namespace CommandCore;
            if (parts.size() != 1)
            {
                return InvalidArguments("profile.record takes no arguments");
            }
            ce::profiler_service& service = ce::profiler();
            if (!service.is_initialized())
            {
                return PreconditionFailed("profile.unavailable", "Profiler is not initialized");
            }
            const ce::recorder_state state = service.state();
            if (state == ce::recorder_state::recording || state == ce::recorder_state::starting)
            {
                return PreconditionFailed("profile.already_recording", "A recording is already active or starting");
            }
            const auto status = service.recording_status();
            if (state == ce::recorder_state::pausing || (!service.recording_path().empty() &&
                (status.state == ce::recording_state::starting || status.state == ce::recording_state::flushing)))
            {
                return PreconditionFailed("profile.finalizing", "The previous recording is still finalizing");
            }
            service.record(service.summary().engine_frame);
            return Ok("New recording requested", ProfileStatePayload());
        }

        static CommandCore::CommandResult Cmd_profile_pause(const std::vector<std::string>& parts)
        {
            using namespace CommandCore;
            if (parts.size() != 1)
            {
                return InvalidArguments("profile.pause takes no arguments");
            }
            if (ce::profiler().state() == ce::recorder_state::starting)
            {
                return PreconditionFailed("profile.starting", "The recording is still starting");
            }
            ce::profiler().pause();
            return Ok("Stop requested; profile.save status reports writer finalization", ProfileStatePayload());
        }

        static CommandCore::CommandResult Cmd_profile_save(const std::vector<std::string>& parts)
        {
            using namespace CommandCore;
            if (parts.size() != 2)
            {
                return InvalidArguments("profile.save <new-absolute-path.ceprof>|status");
            }
            if (parts[1] == "status")
            {
                const auto operation = ProfileSaveState();
                auto data = ProfileSavePayload();
                if (operation && operation->done.load(std::memory_order_acquire) && !operation->error.empty())
                {
                    return Fail("profile.save_failed", operation->error, std::move(data));
                }
                return Ok({}, std::move(data));
            }
            const auto path = std::filesystem::u8path(parts[1]);
            if (!path.is_absolute() || path.extension() != ".ceprof")
            {
                return InvalidArguments("Use a new absolute .ceprof path");
            }
            const auto previous = ProfileSaveState();
            if (previous && !previous->done.load(std::memory_order_acquire))
            {
                return PreconditionFailed("profile.save_pending", "A save is running; use profile.save status");
            }
            ce::profiler_service& service = ce::profiler();
            const ce::recorder_state state = service.state();
            if (state == ce::recorder_state::recording || state == ce::recorder_state::starting ||
                state == ce::recorder_state::pausing)
            {
                return PreconditionFailed("profile.recording", "Stop the capture and wait for writer finalization before saving");
            }
            const auto source = service.recording_path();
            if (source.empty())
            {
                return PreconditionFailed("profile.empty", "No continuous recording to save");
            }
            const auto status = service.recording_status();
            if (status.state != ce::recording_state::finalized)
            {
                return PreconditionFailed("profile.not_finalized", "Writer is not finalized; inspect profile.save status");
            }
            auto operation = std::make_shared<ProfileSaveOperation>();
            operation->path = parts[1];
            ProfileSaveState() = operation;
            try
            {
                const auto admission = std::make_shared<ProfileSaveAdmission>(operation);
                (void)ce::get_job_scheduler().submit([operation, source, path, admission]
                {
                    admission->started.store(true, std::memory_order_release);
                    operation->error.clear();
                    try
                    {
                        std::error_code error;
                        const bool exists = std::filesystem::exists(path, error);
                        if (error || exists)
                        {
                            operation->error = "Destination must be a new writable absolute .ceprof path";
                        }
                        else
                        {
                            const auto recording = ce::open_capture_recording(source);
                            if (!recording)
                            {
                                operation->error = ce::describe(recording.error());
                            }
                            else if (!(*recording)->finalized())
                            {
                                operation->error = "Source recording is not finalized";
                            }
                            else
                            {
                                operation->frames = (*recording)->frame_count();
                                operation->complete = (*recording)->complete();
                                const auto metadata = (*recording)->metadata();
                                operation->unacked = metadata ? metadata->unacked_streams() : 0;
                                if (const auto saved = ce::save_recording(**recording, path); !saved)
                                {
                                    operation->error = ce::describe(saved.error());
                                }
                            }
                        }
                    }
                    catch (const std::exception& exception)
                    {
                        operation->error = exception.what();
                    }
                    catch (...)
                    {
                        operation->error = "Unexpected recording save failure";
                    }
                    operation->done.store(true, std::memory_order_release);
                });
            }
            catch (const std::exception& exception)
            {
                if (!operation->done.load(std::memory_order_acquire))
                {
                    operation->error = exception.what();
                    operation->done.store(true, std::memory_order_release);
                }
                return Fail("profile.save_start_failed", operation->error, ProfileSavePayload());
            }
            if (operation->done.load(std::memory_order_acquire) && !operation->error.empty())
            {
                return Fail("profile.save_failed", operation->error, ProfileSavePayload());
            }
            return Ok("Entire recording save queued; use profile.save status for completion", ProfileSavePayload());
        }

#endif

		// ── 표 ──────────────────────────────────────────────────────────

        CommandCore::CommandResult Cmd_temporal(const std::vector<std::string>& parts)
        {
            return CommandCore::ExecuteTemporalCommand(parts, TemporalPresentationTarget::PlayerSwapchain);
        }

		struct Registration
		{
			const char* name;
			Handler     handler;
		};

		constexpr Registration kPlayerCommands[] = {
            { "temporal.deepdvc", &Cmd_temporal },
            { "temporal.fallback", &Cmd_temporal },
            { "temporal.fg", &Cmd_temporal },
            { "temporal.latency", &Cmd_temporal },
            { "temporal.metadata", &Cmd_temporal },
            { "temporal.motion", &Cmd_temporal },
            { "temporal.nis", &Cmd_temporal },
            { "temporal.reflex", &Cmd_temporal },
            { "temporal.reset", &Cmd_temporal },
            { "temporal.runtime", &Cmd_temporal },
            { "temporal.status", &Cmd_temporal },
            { "temporal.support", &Cmd_temporal },
            { "temporal.upscale", &Cmd_temporal },
			#if !CE_SHIPPING
            { "profile.record", &Cmd_profile_record },
            { "profile.pause", &Cmd_profile_pause },
            { "profile.save", &Cmd_profile_save },
            { "player.device-remove", &Cmd_device_remove },
#endif
            { "help",           &Cmd_help },
			{ "quit",           &Cmd_quit },
			{ "player.status",  &Cmd_status },
			{ "player.scene",   &Cmd_scene },
			{ "player.objects", &Cmd_objects },
			{ "player.object",  &Cmd_object },
			{ "player.animation", &Cmd_animation },
			{ "player.move",    &Cmd_move },
		};

		std::unordered_map<std::string, Handler>& Table()
		{
			static std::unordered_map<std::string, Handler> table;
			return table;
		}
	}

    bool ParseCommandLineArgument(int argc, wchar_t* const* argv, int& index, std::string& error)
    {
        const std::wstring_view option(argv[index]);
        if (!IsLocalCommandArgument(option))
        {
            return false;
        }
        try
        {
            if (option == L"--fail-fast")
            {
                g_batch.failFast = true;
                g_batch.modifiers = true;
                return true;
            }
            if (index + 1 >= argc)
            {
                error = Utf8(argv[index]) + " requires a value";
                return true;
            }
            if (option == L"--result-format")
            {
                g_batch.modifiers = true;
                if (std::wstring_view(argv[++index]) != L"jsonl")
                {
                    error = "Player --result-format must be jsonl";
                }
                return true;
            }
            if (option == L"--result-file")
            {
                g_batch.modifiers = true;
                g_batch.resultPath = argv[++index];
                if (g_batch.resultPath.empty())
                {
                    error = "--result-file requires a nonempty path";
                }
                return true;
            }
            const bool commandlet = option == L"--commandlet" || option == L"--commandlet-script";
            if ((commandlet && g_batch.requested) || (!commandlet && g_batch.commandlet))
            {
                error = "Use one commandlet input, without --exec/--script";
                return true;
            }
            g_batch.requested = true;
            g_batch.commandlet = commandlet;
            if (option == L"--exec")
            {
                AddBatchLine(Utf8(argv[++index]), error);
            }
            else if (option == L"--script" || option == L"--commandlet-script")
            {
                LoadBatchScript(std::filesystem::path(argv[++index]), error);
            }
            else
            {
                std::vector<std::string> arguments;
                for (++index; index < argc; ++index)
                {
                    if (std::wstring_view(argv[index]) == L"--")
                    {
                        break;
                    }
                    arguments.push_back(Utf8(argv[index]));
                }
                if (arguments.empty() || arguments[0].empty())
                {
                    error = "Player command input requires a command name";
                }
                else
                {
                    g_batch.commands.push_back(std::move(arguments));
                }
            }
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
        }
        return true;
    }

    bool ValidateCommandLine(bool commandService, bool smoke, std::string& error)
    {
        if (g_batch.modifiers && !g_batch.requested)
        {
            error = "Player result/fail-fast options require --exec, --script, or --commandlet input";
            return false;
        }
        if (g_batch.requested && (commandService || smoke))
        {
            error = "Player local batch/commandlets cannot be combined with --command-service or --smoke";
            return false;
        }
        if (g_batch.requested && g_batch.commands.empty())
        {
            error = "Player command input is empty";
            return false;
        }
        return true;
    }

    void CommandHost::StartBatch(bool runtimeReady)
    {
        if (!g_batch.requested || g_batch.started)
        {
            return;
        }
        g_batch.started = true;
        if (!runtimeReady)
        {
            g_batch.completed = true;
            RequestQuit();
            return;
        }
        EnsureRegistered();
        CommandCore::CommandSession::Batch().SetFailFast(g_batch.failFast);
        // Preserve redirected handles. GUI-subsystem executables attach to the invoking
        // terminal only when there is no existing stdout pipe or file.
        const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        if (output == nullptr || output == INVALID_HANDLE_VALUE)
        {
            if (AttachConsole(ATTACH_PARENT_PROCESS))
            {
                FILE* stream = nullptr;
                freopen_s(&stream, "CONOUT$", "w", stdout);
                freopen_s(&stream, "CONOUT$", "w", stderr);
            }
        }
        if (!g_batch.resultPath.empty())
        {
            g_batch.resultFile.open(g_batch.resultPath, std::ios::binary | std::ios::trunc);
            if (!g_batch.resultFile)
            {
                CommandCore::CommandSession::Batch().Record("--result-file",
                    CommandCore::InternalError("result.open_failed", "Cannot open Player result file"));
                std::fputs("[PLAYER CLI] Cannot open result file\n", stderr);
                RequestQuit();
                return;
            }
        }
    }

	CommandHost& CommandHost::Get()
	{
		static CommandHost host;
		return host;
	}

	void CommandHost::EnsureRegistered()
	{
		if (m_registered.exchange(true, std::memory_order_acq_rel)) return;

		CommandCore::CommandRegistry& registry = CommandCore::CommandRegistry::Get();

		for (const Registration& entry : kPlayerCommands)
		{
			const CommandCore::DescriptorSeed* seed =
				CommandCore::FindDescriptorSeed(entry.name);

			// ★ **seed 가 없으면 등록하지 않는다.** Editor 와 같은 규약이다 —
			//   서명이 요구하지 않으면 아무도 안 쓰고, 요약 없는 명령이 help 에서
			//   빈 줄이 된다(LC3 이 205 개 중 78 개로 겪었다).
			if (nullptr == seed)
			{
				registry.RecordRejectedName(entry.name, entry.name);
				std::printf("[PLAYER] seed 없는 명령은 등록하지 않는다: %s\n", entry.name);
				continue;
			}

			// ★★ **role 이 이 호스트를 포함해야 등록한다**(§11.2).
			//
			//   이것이 "roles 에 Player 가 없는 명령은 Player registry 에 부재" 를
			//   만드는 한 줄이다. 표에 이름을 적는 것만으로는 들어오지 못한다 —
			//   누군가 나중에 에디터 저작 명령을 이 표에 얹어도 seed 의 role 이
			//   막는다.
			if (!CommandCore::HasRole(seed->roles, CommandCore::CommandRoles::Player))
			{
				std::printf("[PLAYER] roles 에 Player 가 없어 등록하지 않는다: %s\n",
					entry.name);
				continue;
			}

			CommandCore::CommandDescriptor descriptor;
			descriptor.canonical        = entry.name;
			descriptor.summary          = seed->summary;
			descriptor.usage            = seed->usage;
			descriptor.cost             = seed->cost;
			descriptor.cls              = seed->cls;
			descriptor.liveness         = seed->liveness;
			descriptor.roles            = seed->roles;
			descriptor.executesUserCode = seed->executesUserCode;
			descriptor.resultBearing    = true;   // Player 에 legacy 핸들러는 없다

			Table().emplace(entry.name, entry.handler);
			registry.Add(std::move(descriptor));
		}

		std::printf("[PLAYER] 명령 %zu 개 등록 (registry %zu)\n",
			Table().size(), registry.CommandCount());
	}

	CommandCore::CommandResult CommandHost::Execute(const std::vector<std::string>& arguments)
	{
		if (arguments.empty()) return CommandCore::Ok();

		const auto& table = Table();
		const auto it = table.find(arguments[0]);
		if (it == table.end())
		{
			return CommandCore::InvalidArguments(
				"알 수 없는 명령: " + arguments[0] + "  ('help' 참고)", "command.unknown");
		}

		// 핸들러가 던지면 명령의 실패가 아니라 내부 결함이다. Editor 와 같은
		// 경계를 둔다 — 요청 하나가 실행 중인 게임을 죽이면 이 계층의 값어치가
		// 사라진다(§11.3 이 노리는 것은 "재현이 어려운 상태 위에서" 시험하는 것이다).
		try
		{
			return it->second(arguments);
		}
		catch (const std::exception& error)
		{
			return CommandCore::InternalError("command.exception",
				std::string("핸들러 예외: ") + error.what());
		}
		catch (...)
		{
			return CommandCore::InternalError("command.exception", "핸들러에서 알 수 없는 예외");
		}
	}

	bool CommandHost::Enqueue(std::vector<std::string> arguments, Completion completion,
	                          std::size_t queueCap)
	{
		if (arguments.empty()) return false;

		Pending pending;
		pending.arguments     = std::move(arguments);
		pending.enqueuedAt    = std::chrono::steady_clock::now();
		pending.enqueuedFrame = m_frameIndex.load(std::memory_order_acquire);
		pending.completion    = std::move(completion);

		std::lock_guard<std::mutex> guard(m_mutex);

		// ★ 상한 검사와 적재가 **같은 락 안**에 있다. Editor 가 같은 자리에서
		//   배운 것이다 — 떼어 놓으면 동시 요청이 전부 검사를 통과한 뒤 차례로
		//   들어와 상한을 넘긴다.
		if (0 != queueCap && m_pending.size() >= queueCap) return false;

		m_pending.push_back(std::move(pending));
		return true;
	}

	void CommandHost::Pump()
	{
        if (IsQuitRequested())
        {
            return;
        }
        QueueNextBatchCommand();
		const uint64_t frameIndex = m_frameIndex.fetch_add(1, std::memory_order_acq_rel) + 1;

		// 예산. Editor 의 서비스 큐와 같은 뜻이다(§7.2) — 한 프레임이 큐 전체를
		// 소진하면 그 프레임이 통째로 길어지고, 그것은 실행 중인 게임에서
		// 눈에 보이는 끊김이다.
		constexpr std::size_t kDrainCount = 8;
		constexpr double      kDrainBudgetMs = 2.0;

		const auto pumpStarted = std::chrono::steady_clock::now();

		for (std::size_t drained = 0; drained < kDrainCount; ++drained)
		{
			Pending pending;
			{
				std::lock_guard<std::mutex> guard(m_mutex);
				if (m_pending.empty()) return;
				pending = std::move(m_pending.front());
				m_pending.pop_front();
			}

			const auto dequeuedAt = std::chrono::steady_clock::now();
			{
				std::lock_guard<std::mutex> guard(m_statusMutex);
				m_currentCommand = pending.arguments[0];
			}
			m_executing.store(true, std::memory_order_release);

			const CommandCore::CommandResult result = Execute(pending.arguments);

			const auto finishedAt = std::chrono::steady_clock::now();
			m_executing.store(false, std::memory_order_release);
			{
				std::lock_guard<std::mutex> guard(m_statusMutex);
				m_currentCommand.clear();
			}

			if (pending.completion)
			{
				Timing timing;
				timing.queuedMs = std::chrono::duration<double, std::milli>(
					dequeuedAt - pending.enqueuedAt).count();
				timing.executedMs = std::chrono::duration<double, std::milli>(
					finishedAt - dequeuedAt).count();
				timing.waitedFrames = static_cast<uint32_t>(
					(frameIndex > pending.enqueuedFrame)
						? (frameIndex - pending.enqueuedFrame) : 0);
				pending.completion(result, timing);
			}

			const double elapsedMs = std::chrono::duration<double, std::milli>(
				finishedAt - pumpStarted).count();
			if (elapsedMs >= kDrainBudgetMs) return;
		}
	}

	std::size_t CommandHost::QueueDepth() const
	{
		std::lock_guard<std::mutex> guard(m_mutex);
		return m_pending.size();
	}

	CommandHost::Status CommandHost::Snapshot() const
	{
		Status status;
		status.frame     = m_frameIndex.load(std::memory_order_acquire);
		status.executing = m_executing.load(std::memory_order_acquire);
		{
			std::lock_guard<std::mutex> guard(m_statusMutex);
			status.currentCommand = m_currentCommand;
		}
		{
			std::lock_guard<std::mutex> guard(m_mutex);
			status.queueDepth = m_pending.size();
			if (!m_pending.empty())
			{
				status.oldestQueuedMs = std::chrono::duration<double, std::milli>(
					std::chrono::steady_clock::now() - m_pending.front().enqueuedAt).count();
			}
		}
		return status;
	}
}

#else
namespace PlayerCmd
{
    bool ParseCommandLineArgument(int, wchar_t* const* argv, int& index, std::string& error)
    {
        if (!IsLocalCommandArgument(argv[index]))
        {
            return false;
        }
        error = "Player CLI and commandlets require a Development Build (EngineShipping=false)";
        return true;
    }

    bool ValidateCommandLine(bool, bool, std::string&)
    {
        return true;
    }
}
#endif // CE_DEVELOPMENT
