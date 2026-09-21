#include "ProfileMarker.h"

#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// 유니티 빌드가 켜져 있으므로 익명 네임스페이스를 쓰지 않는다 — 격리 단위가
// 파일이 아니라 blob 이라 같은 이름이 남의 청크와 충돌한다. 이름 있는
// 네임스페이스로 격리한다(계획서 §14 · 저장소 규약).
namespace ce::detail::marker_registry_impl
{
	// 표는 자라기만 한다. 등록 해제가 없으므로 reader 는 한 번 받은 인덱스가
	// 뒤집히지 않는다고 믿을 수 있고, 그래서 이벤트가 id 만 들고 다녀도 된다.
	struct registry
	{
		std::mutex                                     lock;
		std::vector<marker_desc>                       descs;
		std::unordered_map<std::string, marker_id>     by_name;

		// 런타임 이름의 보관소. deque 인 이유는 **주소가 고정**되어야 하기
		// 때문이다 — vector<string> 은 재할당 때 원소를 옮기고, 짧은 문자열은
		// 버퍼가 객체 안에 있어(SSO) 같이 움직인다. 그러면 표가 들고 있는
		// c_str() 가 통째로 엉뚱한 곳을 가리킨다.
		std::deque<std::string>                        owned_names;
	};

	// 함수 지역 static — 첫 호출에서 선다. 마커 슬롯의 동적 초기화가 어느
	// 순서로 깨어나든(TU 간 순서는 미정의다) 이 표는 그보다 먼저 완성된다.
	inline registry& get()
	{
		static registry instance;
		return instance;
	}

	// id 0 은 invalid_marker 로 비워 둔다. descs[0] 을 자리표로 채워
	// marker_info(invalid_marker) 가 경계 검사 없이 안전한 값을 돌려준다.
	inline void ensure_sentinel(registry& reg)
	{
		if (reg.descs.empty())
		{
			reg.descs.push_back(marker_desc{ "<invalid>", nullptr, 0, marker_kind::cpu_scope });
		}
	}
}

namespace ce
{
	namespace detail
	{
		marker_id intern_marker(const marker_desc& desc)
		{
			using namespace ce::detail::marker_registry_impl;

			registry& reg = get();
			std::lock_guard<std::mutex> guard(reg.lock);
			ensure_sentinel(reg);

			// 같은 이름은 같은 id 다. 슬롯이 이름마다 하나라 보통은 여기서
			// 한 번만 지나가지만, 이름을 공유하는 서로 다른 kind 가 들어오면
			// 표를 나눠야 하므로 키에 kind 를 섞는다.
			std::string key(desc.name ? desc.name : "");
			key.push_back('#');
			key.push_back(static_cast<char>('0' + static_cast<int>(desc.kind)));

			const auto found = reg.by_name.find(key);
			if (found != reg.by_name.end())
			{
				return found->second;
			}

			const marker_id id = static_cast<marker_id>(reg.descs.size());
			reg.descs.push_back(desc);
			reg.by_name.emplace(std::move(key), id);
			return id;
		}
	}

	marker_id intern_runtime_marker(std::string_view name, marker_kind kind)
	{
		using namespace ce::detail::marker_registry_impl;

		registry& reg = get();
		std::lock_guard<std::mutex> guard(reg.lock);
		ensure_sentinel(reg);

		std::string key(name);
		key.push_back('#');
		key.push_back(static_cast<char>('0' + static_cast<int>(kind)));

		const auto found = reg.by_name.find(key);
		if (found != reg.by_name.end())
		{
			return found->second;
		}

		// 표가 이름을 소유한다. 이 주소는 프로세스가 끝날 때까지 산다.
		reg.owned_names.emplace_back(name);
		const char* stable = reg.owned_names.back().c_str();

		const marker_id id = static_cast<marker_id>(reg.descs.size());
		reg.descs.push_back(marker_desc{ stable, nullptr, 0, kind });
		reg.by_name.emplace(std::move(key), id);
		return id;
	}

	const marker_desc& marker_info(marker_id id)
	{
		using namespace ce::detail::marker_registry_impl;

		registry& reg = get();
		std::lock_guard<std::mutex> guard(reg.lock);
		ensure_sentinel(reg);

		if (id >= reg.descs.size())
		{
			return reg.descs[invalid_marker];
		}
		return reg.descs[id];
	}

	std::span<const marker_desc> registered_markers()
	{
		using namespace ce::detail::marker_registry_impl;

		// 표가 자라기만 하고 원소를 옮기는 것은 vector 재할당뿐이다. reader 가
		// 등록과 겹치면 옛 저장소를 볼 수 있으므로, 호출자는 등록이 멈춘 뒤에만
		// 쓴다(수집이 끝난 capture 를 읽을 때가 그 자리다).
		registry& reg = get();
		std::lock_guard<std::mutex> guard(reg.lock);
		ensure_sentinel(reg);
		return std::span<const marker_desc>(reg.descs.data(), reg.descs.size());
	}

	std::uint32_t registered_marker_count()
	{
		using namespace ce::detail::marker_registry_impl;

		registry& reg = get();
		std::lock_guard<std::mutex> guard(reg.lock);
		ensure_sentinel(reg);
		// 자리표를 뺀 실제 등록 수.
		return static_cast<std::uint32_t>(reg.descs.size() - 1);
	}
}
