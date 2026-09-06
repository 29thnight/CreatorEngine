#include "ScriptComponent.h"
#include "ClrHost.h"
#include "Entity.h"

namespace
{
	// 항목은 "타입|값|이름"으로 한 줄에 담긴다. 문자열 값에 구분자나 개행이 들어오면
	// 경계가 깨지므로 최소한으로 이스케이프한다(YAML에 그대로 실려도 안전한 형태).
	std::string EscapeValue(const std::string& raw)
	{
		std::string out;
		out.reserve(raw.size());

		for (char c : raw)
		{
			switch (c)
			{
			case '\\': out += "\\\\"; break;
			case '|':  out += "\\p";  break;
			case '\n': out += "\\n";  break;
			case '\r': out += "\\r";  break;
			default:   out += c;      break;
			}
		}
		return out;
	}

	std::string UnescapeValue(const std::string& escaped)
	{
		std::string out;
		out.reserve(escaped.size());

		for (size_t i = 0; i < escaped.size(); ++i)
		{
			if ('\\' != escaped[i] || i + 1 >= escaped.size())
			{
				out += escaped[i];
				continue;
			}

			switch (escaped[++i])
			{
			case '\\': out += '\\'; break;
			case 'p':  out += '|';  break;
			case 'n':  out += '\n'; break;
			case 'r':  out += '\r'; break;
			default:   out += escaped[i]; break;
			}
		}
		return out;
	}
}

void ScriptComponent::EnsureInstance()
{
	if (m_scriptType.empty())
	{
		return;   // 타입이 아직 정해지지 않은 빈 컴포넌트(인스펙터에서 막 추가한 상태)
	}

	if (HasInstance())
	{
		return;   // 재생/정지 왕복 등으로 다시 불려도 인스턴스는 하나만 만든다
	}

	if (m_instanceCreateFailed)
	{
		// 이미 한 번 실패했다. 편집 모드에서는 드레인이 매 프레임 이 함수를
		// 부르므로 여기서 멈추지 않으면 같은 실패가 프레임마다 쌓인다.
		// 다시 시도하려면 RetryInstance를 부른다.
		return;
	}

	auto& clr = ClrHost::Get();
	if (!clr.IsReady())
	{
		m_instanceCreateFailed = true;
		return;
	}

	m_instanceId = clr.CreateComponent(GetOwner(), m_scriptType);
	if (!HasInstance())
	{
		m_instanceCreateFailed = true;
		return;
	}

	// 저장돼 있던 값을 되돌린다. 인스펙터가 편집 모드에 이 값을 보여 주고,
	// 재생에서 관리 측 OnInitialized가 그 값을 그대로 읽는다.
	ApplyFields();
}

void ScriptComponent::RetryInstance()
{
	m_instanceCreateFailed = false;
	EnsureInstance();
}

void ScriptComponent::OnInitialized()
{
	++m_initializationAttempts;
	if (m_scriptType.empty())
	{
		return;
	}

	EnsureInstance();

	if (!HasInstance())
	{
		// ★ 이 로그가 곧 "OnInitialized가 몇 번 불렸는가"다.
		//
		// verify-script-add-awake-once가 이 줄 수를 세어 이중 초기화를 잡는다
		// (등록되지 않은 타입을 붙여 HasInstance가 절대 참이 되지 않게 한다).
		// 그래서 이 로그는 EnsureInstance가 아니라 **여기** 있어야 한다 —
		// EnsureInstance는 실패를 기억해 두 번째부터 조용히 반환하므로,
		// 거기서 찍으면 이중 호출이 한 줄로 보여 그 게이트가 눈멀게 된다.
		auto& clr = ClrHost::Get();
		if (!clr.IsReady())
		{
			Debug->LogWarning("[스크립트] CLR이 준비되지 않아 " + m_scriptType + " 을 만들지 못했습니다");
		}
		else
		{
			Debug->LogError("[스크립트] 인스턴스 생성 실패 — 등록되지 않은 타입: " + m_scriptType);
		}
		return;
	}

	// 관리 측 OnInitialized를 여기서 부른다(트랙 L · L3 잔여 2단계). 예전에는
	// ScriptRegistry가 자기 큐(_pendingAwake)로 다음 틱에 불렀는데, 그러면
	// 관리 측 생명주기의 드라이버가 네이티브와 둘이 된다 — 그 이원화가 DDOL 이송
	// 신호가 스크립트에 닿지 않던 원인이었다(ScriptLifecyclePhase.h).
	//
	// 이 함수가 편집 모드에서 불리지 않는 것은 Scene::DrainPendingPhases가
	// 보장한다 — 그쪽은 EnsureInstance만 부르고 컴포넌트를 큐에 되돌린다.
	NotifyManagedLifecycle(ScriptLifecyclePhase::OnInitialized);
}

void ScriptComponent::PrepareForReload()
{
	// 값을 먼저 챙긴다 — 인스턴스가 사라지면 읽을 수 없다.
	CaptureFields();

	// 관리 측이 통째로 내려가므로 여기서 파괴를 요청할 필요가 없다.
	// id만 버려서 다음 EnsureInstance가 새로 만들게 한다. 실패 기억도 함께
	// 지운다 — 리로드는 "등록되지 않은 타입"이 등록되는 대표적 계기다.
	m_instanceId = -1;
	m_instanceCreateFailed = false;
}

void ScriptComponent::RestoreAfterReload()
{
	// ⓪ 편집 모드에서는 인스턴스만 되살린다.
	//
	// 편집 모드에서 관리 훅을 돌리지 않는 것은 규약이고(bd13620c), 그 가드는
	// Scene의 드레인 쪽에 있다 — 이 함수는 CLI가 직접 부르므로 그 가드를 지나지
	// 않는다. 여기서 가르지 않으면 편집 중 리로드가 OnInitialized·OnAddedToScene·
	// OnEnable을 관리 측으로 흘린다.
	//
	// State_Initialized가 곧 "재생을 지났는가"다. 편집 모드에서는 드레인이 훅을
	// 돌리지 않고 PendingInitialize로 되돌리므로 이 비트가 서지 않는다. 리로드는
	// 이 비트를 지우지 않으니 리로드 전의 사실을 그대로 쥐고 있다.
	//
	// 인스턴스는 편집 모드에도 있어야 한다 — 인스펙터의 [SerializeField] 편집이
	// 이 인스턴스를 통해서만 동작한다.
	if (!HasLifecycleState(State_Initialized))
	{
		EnsureInstance();
		return;
	}

	// ① 인스턴스를 다시 만들고 챙겨 둔 값을 되돌린다.
	OnInitialized();

	// 인스턴스가 안 만들어졌으면 나머지를 이어도 관리 측에 닿지 않는다. 리로드는
	// "등록되지 않은 타입"이 등록되는 계기이기도 하지만, 반대로 타입이 사라지는
	// 계기이기도 하다 — 그때 조용히 여기서 멈춘다(OnInitialized가 이미 로그를 남겼다).
	if (!HasInstance())
	{
		return;
	}

	// ② 씬 편입. OnAddedToScene에는 1회 가드 비트가 없다 — 씬에 편입될 때마다
	//    발화하는 것이 계약이라(DDOL 이송의 OnRemovingFromScene과 대칭짝) 여기서
	//    부르는 것이 그 계약과 어긋나지 않는다.
	OnAddedToScene();

	// ③ 활성. 정상 진입 순서가 added → enable → begin이라 그 자리에 둔다.
	if (IsEnabled())
	{
		OnEnable();
	}

	// ④ 시뮬레이션. 리로드 전에 시작했었을 때만 잇는다 — 재생을 지났더라도 정지
	//    상태라면 여기까지 오지 않는다. State_SimulationBegun도 리로드가 지우지
	//    않으므로 그 시점의 사실을 그대로 쥐고 있다.
	if (HasLifecycleState(State_SimulationBegun))
	{
		OnBeginSimulation();
	}
}

void ScriptComponent::CaptureFields()
{
	m_fieldData.clear();

	if (!HasInstance())
	{
		return;   // 인스턴스가 없으면 기존 값을 그대로 둔다(위에서 clear 했으므로 복구)
	}

	auto& clr = ClrHost::Get();
	const int count = clr.GetFieldCount(m_instanceId);
	m_fieldData.reserve(static_cast<size_t>(count));

	for (int i = 0; i < count; ++i)
	{
		const auto type = clr.GetFieldType(m_instanceId, i);
		const std::string name = clr.GetFieldName(m_instanceId, i);

		std::string value;
		switch (type)
		{
		case ClrHost::ScriptFieldType::Float:
			value = std::to_string(clr.GetFieldFloat(m_instanceId, i));
			break;
		case ClrHost::ScriptFieldType::Int32:
			value = std::to_string(clr.GetFieldInt32(m_instanceId, i));
			break;
		case ClrHost::ScriptFieldType::Bool:
			value = clr.GetFieldBool(m_instanceId, i) ? "1" : "0";
			break;
		case ClrHost::ScriptFieldType::Float2:
		{
			const auto v = clr.GetFieldFloat2(m_instanceId, i);
			value = std::to_string(v.x) + "," + std::to_string(v.y);
			break;
		}
		case ClrHost::ScriptFieldType::Float3:
		{
			const auto v = clr.GetFieldFloat3(m_instanceId, i);
			value = std::to_string(v.x) + "," + std::to_string(v.y) + "," + std::to_string(v.z);
			break;
		}
		case ClrHost::ScriptFieldType::String:
		{
			// 값 안의 개행·구분자가 항목 경계를 깨지 않도록 이스케이프한다.
			value = EscapeValue(clr.GetFieldString(m_instanceId, i));
			break;
		}
		case ClrHost::ScriptFieldType::Object:
		{
			// 핸들의 슬롯 번호는 실행마다 달라지므로 남길 수 없다.
			// 씬 직렬화가 보존하는 instanceID를 적어 둔다.
			Entity* target = clr.GetFieldObject(m_instanceId, i);
			value = (nullptr != target) ? std::to_string(target->GetInstanceID()) : "0";
			break;
		}
		default:
			continue;   // 아직 다루지 않는 타입은 저장하지 않는다
		}

		m_fieldData.push_back(std::to_string(static_cast<int>(type)) + "|" + value + "|" + name);
	}
}

void ScriptComponent::ApplyFields()
{
	if (!HasInstance() || m_fieldData.empty())
	{
		return;
	}

	auto& clr = ClrHost::Get();
	const int count = clr.GetFieldCount(m_instanceId);

	for (const std::string& entry : m_fieldData)
	{
		// "타입|값|이름" — 앞의 두 칸만 끊고 나머지는 전부 이름으로 본다.
		const size_t firstBar = entry.find('|');
		if (std::string::npos == firstBar) continue;

		const size_t secondBar = entry.find('|', firstBar + 1);
		if (std::string::npos == secondBar) continue;

		const int type = std::atoi(entry.substr(0, firstBar).c_str());
		const std::string value = entry.substr(firstBar + 1, secondBar - firstBar - 1);
		const std::string name = entry.substr(secondBar + 1);

		// 이름으로 현재 인스턴스의 필드를 찾는다.
		// 인덱스를 저장하지 않는 이유는 스크립트를 고쳐 필드 순서가 바뀌어도
		// 값이 엉뚱한 곳에 들어가지 않게 하기 위해서다.
		int index = -1;
		for (int i = 0; i < count; ++i)
		{
			if (clr.GetFieldName(m_instanceId, i) == name)
			{
				index = i;
				break;
			}
		}

		if (index < 0)
		{
			continue;   // 없어진 필드 — 조용히 버린다
		}

		// 타입이 바뀐 경우에도 값을 밀어 넣지 않는다.
		if (clr.GetFieldType(m_instanceId, index) != static_cast<ClrHost::ScriptFieldType>(type))
		{
			continue;
		}

		switch (static_cast<ClrHost::ScriptFieldType>(type))
		{
		case ClrHost::ScriptFieldType::Float:
			clr.SetFieldFloat(m_instanceId, index, static_cast<float>(std::atof(value.c_str())));
			break;
		case ClrHost::ScriptFieldType::Int32:
			clr.SetFieldInt32(m_instanceId, index, std::atoi(value.c_str()));
			break;
		case ClrHost::ScriptFieldType::Bool:
			clr.SetFieldBool(m_instanceId, index, "1" == value);
			break;
		case ClrHost::ScriptFieldType::Float2:
		{
			ClrHost::ScriptFloat2 v{};
			const size_t comma = value.find(',');
			if (std::string::npos != comma)
			{
				v.x = static_cast<float>(std::atof(value.substr(0, comma).c_str()));
				v.y = static_cast<float>(std::atof(value.substr(comma + 1).c_str()));
			}
			clr.SetFieldFloat2(m_instanceId, index, v);
			break;
		}
		case ClrHost::ScriptFieldType::Float3:
		{
			ClrHost::ScriptFloat3 v{};
			const size_t firstComma = value.find(',');
			const size_t secondComma = (std::string::npos != firstComma) ? value.find(',', firstComma + 1) : std::string::npos;
			if (std::string::npos != secondComma)
			{
				v.x = static_cast<float>(std::atof(value.substr(0, firstComma).c_str()));
				v.y = static_cast<float>(std::atof(value.substr(firstComma + 1, secondComma - firstComma - 1).c_str()));
				v.z = static_cast<float>(std::atof(value.substr(secondComma + 1).c_str()));
			}
			clr.SetFieldFloat3(m_instanceId, index, v);
			break;
		}
		case ClrHost::ScriptFieldType::String:
			clr.SetFieldString(m_instanceId, index, UnescapeValue(value));
			break;
		case ClrHost::ScriptFieldType::Object:
		{
			// instanceID로 다시 찾는다. 참조 대상이 아직 로드되지 않았거나
			// 삭제된 경우에는 빈 참조로 둔다(스크립트가 IsAlive로 걸러낼 수 있다).
			const size_t targetId = std::strtoull(value.c_str(), nullptr, 10);
			Entity* target = (0 != targetId) ? Entity::FindInstanceID(HashedGuid(targetId)) : nullptr;
			clr.SetFieldObject(m_instanceId, index, target);
			break;
		}
		default:
			break;
		}
	}
}

void ScriptComponent::OnAddedToScene()
{
	NotifyManagedLifecycle(ScriptLifecyclePhase::OnAddedToScene);
}

void ScriptComponent::OnBeginSimulation()
{
	NotifyManagedLifecycle(ScriptLifecyclePhase::OnBeginSimulation);
}

void ScriptComponent::OnEndSimulation()
{
	NotifyManagedLifecycle(ScriptLifecyclePhase::OnEndSimulation);
}

void ScriptComponent::OnRemovingFromScene()
{
	NotifyManagedLifecycle(ScriptLifecyclePhase::OnRemovingFromScene);
}

void ScriptComponent::OnEnable()
{
	if (!HasInstance()) return;
	ClrHost::Get().DispatchEnabled(m_instanceId, true);
}

void ScriptComponent::OnDisable()
{
	if (!HasInstance()) return;
	ClrHost::Get().DispatchEnabled(m_instanceId, false);
}

void ScriptComponent::NotifyManagedLifecycle(ScriptLifecyclePhase phase)
{
	if (!HasInstance())
	{
		return;   // 인스턴스가 없으면 전달할 대상도 없다(타입 미지정·CLR 미준비 등)
	}

	ClrHost::Get().DispatchLifecycle(m_instanceId, phase);
}

void ScriptComponent::OnUninitializing()
{
	if (!HasInstance())
	{
		return;
	}

	// 관리 측 훅을 **먼저** 전달하고 인스턴스를 없앤다. 순서가 뒤집히면 스크립트가
	// 자기 마지막 훅을 못 받는다(트랙 L · L3 완결).
	NotifyManagedLifecycle(ScriptLifecyclePhase::OnUninitializing);

	ClrHost::Get().DestroyComponent(m_instanceId);
	m_instanceId = -1;
}

