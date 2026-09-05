#include "PlayerCommands.h"

#include "CommandCore/CommandDescriptorSeeds.h"
#include "CommandCore/CommandRegistry.h"

#include "Entity.h"
#include "Scene.h"
#include "SceneManager.h"
#include "TimeSystem.h"
#include "Transform.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace PlayerCmd
{
	namespace
	{
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

		// ── 표 ──────────────────────────────────────────────────────────

		struct Registration
		{
			const char* name;
			Handler     handler;
		};

		constexpr Registration kPlayerCommands[] = {
			{ "help",           &Cmd_help },
			{ "quit",           &Cmd_quit },
			{ "player.status",  &Cmd_status },
			{ "player.scene",   &Cmd_scene },
			{ "player.objects", &Cmd_objects },
			{ "player.object",  &Cmd_object },
			{ "player.move",    &Cmd_move },
		};

		std::unordered_map<std::string, Handler>& Table()
		{
			static std::unordered_map<std::string, Handler> table;
			return table;
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
