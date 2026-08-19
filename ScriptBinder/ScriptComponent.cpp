#ifndef DYNAMICCPP_EXPORTS
#include "ScriptComponent.h"
#include "ClrHost.h"
#include "GameObject.h"

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

void ScriptComponent::OnInitialized()
{
	if (m_scriptType.empty())
	{
		return;   // 타입이 아직 정해지지 않은 빈 컴포넌트(인스펙터에서 막 추가한 상태)
	}

	if (HasInstance())
	{
		return;   // 재생/정지 왕복 등으로 Awake가 다시 불려도 인스턴스는 하나만 만든다
	}

	auto& clr = ClrHost::Get();
	if (!clr.IsReady())
	{
		Debug->LogWarning("[스크립트] CLR이 준비되지 않아 " + m_scriptType + " 을 만들지 못했습니다");
		return;
	}

	m_instanceId = clr.CreateBehaviour(GetOwner(), m_scriptType);
	if (!HasInstance())
	{
		Debug->LogError("[스크립트] 인스턴스 생성 실패 — 등록되지 않은 타입: " + m_scriptType);
		return;
	}

	// 저장돼 있던 값을 되돌린다. C# 쪽 Awake는 아직 불리지 않았으므로
	// 스크립트가 Awake에서 필드를 읽어도 복원된 값을 본다.
	ApplyFields();
}

void ScriptComponent::PrepareForReload()
{
	// 값을 먼저 챙긴다 — 인스턴스가 사라지면 읽을 수 없다.
	CaptureFields();

	// 관리 측이 통째로 내려가므로 여기서 파괴를 요청할 필요가 없다.
	// id만 버려서 다음 Awake가 새로 만들게 한다.
	m_instanceId = -1;
}

void ScriptComponent::SuspendInstance()
{
	if (!HasInstance()) return;

	// 값을 먼저 챙긴다 — 인스턴스가 사라지면 읽을 수 없다.
	CaptureFields();

	ClrHost::Get().DestroyBehaviour(m_instanceId);
	m_instanceId = -1;
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

	ClrHost::Get().DestroyBehaviour(m_instanceId);
	m_instanceId = -1;
}
#endif // !DYNAMICCPP_EXPORTS

