#pragma once
#include "AuthoringScalarConvert.h"

#include <yaml-cpp/yaml.h>
#include <ryml/ryml.hpp>
#include <ryml/ryml_std.hpp>

// ★ `ReflectionYml.h`를 물지 않는다 — 그쪽이 이 헤더를 필요로 하므로 순환이 된다.
// 네임스페이스 별칭은 같은 대상을 가리키므로 중복 선언이 허용된다.
namespace MetaYml = YAML;

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

// SerializationPlan D3-b-2b-1b — 읽기 경로가 실제로 쓰는 **노드 연산만** 노출하는
// 어댑터.
//
// ★ 왜 어댑터인가. 파서를 바꾸려면 `LoadFile`부터 `ReadMember`까지 타입이 한꺼번에
//   바뀌어야 한다 — 중간에 끊을 수 없어서 한 번에 수백 곳을 고치게 된다. 그러면
//   무언가 어긋났을 때 "ryml이 다르게 읽은 것"과 "옮기다 틀린 것"을 가를 수 없다
//   (D3-b-0이 파서 프로브를 먼저 만든 이유와 같다).
//
// ── D3-b-2b-1b-3a: 이중 backend ─────────────────────────────────────────────
//
// ★ 한 번에 다 바꿀 수 없다는 것이 실측으로 드러났다. 아직 backend 노드를 요구하는
//   자리가 남아 있고(`DataSystem`은 experiment 코덱에 yaml-cpp 노드를 넘긴다 —
//   I5 소유, `Prefab` 소환은 트리를 변형하는 read-write 경로), 그것들을 억지로 먼저
//   옮기면 ryml에서 다시 써야 하는 코드를 두 번 쓰게 된다.
//
//   그래서 어댑터가 **두 backend를 모두 담을 수 있게** 한다. 씬 로드처럼 옮길
//   준비가 된 원천부터 ryml로 파싱하고 나머지는 yaml-cpp로 남는다. 소비자 코드는
//   어느 쪽인지 모른다 — 그것이 어댑터를 먼저 세운 이유다.
//
// ★ 분기 비용은 연산당 조건 하나다. 파싱 비용(실측상 씬 로드의 60%)에 비하면 무시할
//   수 있고, 이 상태는 전환이 끝나면 사라진다.
//
// ★ **ryml 뷰는 트리를 소유하지 않는다.** `m_tree`가 가리키는 `ryml::Tree`는 호출부가
//   살려 둬야 한다(§8 "ryml view 수명 오용"). yaml-cpp 쪽은 값 의미론이라 이 규칙이
//   필요 없었고, 그 비대칭이 전환에서 가장 위험한 지점이다.
namespace Authoring
{
	class ReadNode
	{
	public:
		ReadNode() = default;

		explicit ReadNode(MetaYml::Node node) noexcept
			: m_backend(Backend::YamlCpp), m_node(std::move(node)) {}

		// ryml 노드로 만든다. **트리는 호출부가 소유한다.**
		[[nodiscard]] static ReadNode FromRyml(const ryml::Tree& tree, std::size_t id) noexcept
		{
			ReadNode node;
			if (id == ryml::NONE) return node;
			node.m_backend = Backend::Ryml;
			node.m_tree = &tree;
			node.m_id = id;
			return node;
		}

		[[nodiscard]] static ReadNode FromRyml(const ryml::Tree& tree) noexcept
		{
			if (tree.empty()) return ReadNode{};
			return FromRyml(tree, tree.root_id());
		}

		// 노드가 문서에 존재하는가. 부재 키는 여기서 걸러진다(레거시 파리티:
		// 부재는 오류가 아니라 "그 필드를 건드리지 않음"이다).
		[[nodiscard]] explicit operator bool() const
		{
			switch (m_backend)
			{
			case Backend::YamlCpp: return static_cast<bool>(m_node);
			case Backend::Ryml:
			case Backend::RymlKey: return nullptr != m_tree && m_id != ryml::NONE;
			default:               return false;
			}
		}

		[[nodiscard]] bool IsNull() const
		{
			switch (m_backend)
			{
			case Backend::YamlCpp: return m_node.IsNull();
			// ryml은 "값이 있는데 그 값이 null 표기"를 따로 묻는다. 값이 없는
			// 노드(맵/시퀀스)는 널이 아니다 — yaml-cpp와 같은 판정이다.
			case Backend::Ryml:    return m_tree->has_val(m_id) && m_tree->val_is_null(m_id);
			default:               return false;
			}
		}

		[[nodiscard]] bool IsScalar() const
		{
			switch (m_backend)
			{
			case Backend::YamlCpp: return m_node.IsScalar();
			// 널 노드는 스칼라가 아니다(yaml-cpp 파리티 — `IsNull()`이 참일 때
			// `IsScalar()`는 거짓이다).
			case Backend::Ryml:    return m_tree->has_val(m_id) && !m_tree->val_is_null(m_id);
			case Backend::RymlKey: return m_tree->has_key(m_id);
			default:               return false;
			}
		}

		[[nodiscard]] bool IsMap() const
		{
			switch (m_backend)
			{
			case Backend::YamlCpp: return m_node.IsMap();
			case Backend::Ryml:    return m_tree->is_map(m_id);
			default:               return false;   // 키 뷰는 언제나 스칼라다
			}
		}

		[[nodiscard]] bool IsSequence() const
		{
			switch (m_backend)
			{
			case Backend::YamlCpp: return m_node.IsSequence();
			case Backend::Ryml:    return m_tree->is_seq(m_id);
			default:               return false;
			}
		}

		[[nodiscard]] std::size_t Size() const
		{
			switch (m_backend)
			{
			case Backend::YamlCpp: return m_node.size();
			case Backend::Ryml:    return m_tree->num_children(m_id);
			default:               return 0;
			}
		}

		// 맵 조회. 없는 키는 **유효하지 않은 노드**를 돌려준다 — 예외가 아니다.
		//
		// ★ ryml에서는 없는 키를 `operator[]`로 만지면 **visit 채널로 abort한다.**
		//   그래서 `find_child`를 쓴다 — 없으면 `NONE`을 돌려주고 죽지 않는다.
		//   이 계약을 어기면 저작 문서에 필드 하나가 빠졌을 때 에디터가 죽는다.
		[[nodiscard]] ReadNode operator[](const char* key) const
		{
			switch (m_backend)
			{
			case Backend::YamlCpp: return ReadNode{ m_node[key] };
			case Backend::Ryml:
			{
				if (!m_tree->is_map(m_id)) return ReadNode{};
				const ryml::id_type child = m_tree->find_child(m_id, ryml::to_csubstr(key));
				if (child == ryml::NONE) return ReadNode{};
				return FromRyml(*m_tree, child);
			}
			default: return ReadNode{};
			}
		}

		[[nodiscard]] bool HasChild(const char* key) const
		{
			switch (m_backend)
			{
			case Backend::YamlCpp: return static_cast<bool>(m_node[key]);
			case Backend::Ryml:
				return m_tree->is_map(m_id)
					&& m_tree->find_child(m_id, ryml::to_csubstr(key)) != ryml::NONE;
			default: return false;
			}
		}

		// 원문 스칼라. 변환은 하지 않는다 — `Authoring::Scalar`의 몫이다.
		//
		// ★ **널 노드는 "null"이다.** yaml-cpp `as<std::string>`의 실측 동작이고
		//   (D3-b-2b-1a가 게이트로 확인했다), ryml 쪽도 같은 규칙을 세운다. 이
		//   규칙을 빠뜨리면 널을 담은 문자열 필드가 조용히 빈 문자열이 된다.
		[[nodiscard]] std::string_view Scalar() const
		{
			switch (m_backend)
			{
			case Backend::YamlCpp:
				if (m_node.IsNull()) return std::string_view{ "null" };
				if (!m_node.IsScalar()) return std::string_view{};
				return std::string_view{ m_node.Scalar() };
			case Backend::Ryml:
			{
				if (!m_tree->has_val(m_id)) return std::string_view{};
				if (m_tree->val_is_null(m_id)) return std::string_view{ "null" };
				const ryml::csubstr value = m_tree->val(m_id);
				return std::string_view{ value.str, value.len };
			}
			case Backend::RymlKey:
			{
				if (!m_tree->has_key(m_id)) return std::string_view{};
				const ryml::csubstr key = m_tree->key(m_id);
				return std::string_view{ key.str, key.len };
			}
			default: return std::string_view{};
			}
		}

		// ── 값 읽기 ──────────────────────────────────────────────────────────
		//
		// yaml-cpp `as<T>()`의 **드롭인 대체**다. 변환은 `Authoring::Scalar`가 하고,
		// 실패하면 던진다 — 예외로 끝나는 것이 이전 동작이고 호출부의 catch 의미가
		// 유지되어야 한다(씬 로드는 `LoadScene`의 catch까지 올라간다).
		//
		// ★ yaml-cpp backend에서는 `as<T>()`를 불러 **예외 타입까지** 그대로 유지한다.
		//   ryml에는 대응물이 없으므로 `std::runtime_error`를 던진다 — 둘 다
		//   `std::exception`이라 기존 catch가 잡는다.
		template<class T>
		[[nodiscard]] T As() const
		{
			T value{};
			if (Authoring::Scalar::TryConvert(Scalar(), value)) return value;
			if (Backend::YamlCpp == m_backend) return m_node.as<T>();
			ThrowConversionFailure();
		}

		// 폴백 형태 — 던지지 않는다. `as<T>(fallback)`의 등가물.
		template<class T>
		[[nodiscard]] T As(T fallback) const
		{
			T value{};
			if (Authoring::Scalar::TryConvert(Scalar(), value)) return value;
			return fallback;
		}

		// 문자열은 변환이 없다 — 원문이 곧 값이다(널은 "null").
		[[nodiscard]] std::string AsString() const { return std::string{ Scalar() }; }

		// ── 시퀀스 순회 ──────────────────────────────────────────────────────
		//
		// 인덱스 기반이다. 두 backend 모두 인덱스 접근이 되고, 반복자 어댑터를 두 벌
		// 유지하는 것보다 옮길 때 어긋날 여지가 적다.
		[[nodiscard]] ReadNode At(std::size_t index) const
		{
			switch (m_backend)
			{
			case Backend::YamlCpp: return ReadNode{ m_node[index] };
			case Backend::Ryml:
			{
				if (index >= m_tree->num_children(m_id)) return ReadNode{};
				return FromRyml(*m_tree, m_tree->child(m_id, index));
			}
			default: return ReadNode{};
			}
		}

		class Iterator
		{
		public:
			Iterator(const ReadNode* owner, std::size_t index) noexcept
				: m_owner(owner), m_index(index) {}

			[[nodiscard]] ReadNode operator*() const { return m_owner->At(m_index); }
			Iterator& operator++() noexcept { ++m_index; return *this; }
			[[nodiscard]] bool operator!=(const Iterator& other) const noexcept
			{
				return m_index != other.m_index;
			}

		private:
			const ReadNode* m_owner;
			std::size_t m_index;
		};

		[[nodiscard]] Iterator begin() const { return Iterator{ this, 0 }; }
		[[nodiscard]] Iterator end() const { return Iterator{ this, Size() }; }

		// 맵 순회는 클래스 밖에 있다 — 항목이 `ReadNode`를 값으로 담는데 중첩 타입
		// 안에서는 아직 불완전 타입이기 때문이다. 아래 `MapRange` 참조.
		[[nodiscard]] class MapRange Map() const;

		// ── 전환기 전용 창구 ─────────────────────────────────────────────────
		//
		// 아직 옮기지 않은 소비자가 backend 노드를 필요로 한다. **전환이 끝나면
		// 사라져야 하는 함수**이므로 이름으로 그 사실을 남긴다 — 개수가 진행률이고
		// `verify-authoring-backend-boundary.ps1`이 센다.
		//
		// ★ ryml 노드에 이것을 부르면 **던진다.** 조용히 빈 노드를 주면 그 자리에서
		//   데이터가 사라지고 아무도 모른다 — 옮기다 만 경로에 ryml 문서가 흘러든
		//   것이므로 시끄럽게 실패해야 한다.
		[[nodiscard]] const MetaYml::Node& BackendNodeDuringTransition() const
		{
			if (Backend::YamlCpp != m_backend) ThrowBackendMismatch();
			return m_node;
		}

		// ★ 뷰 표현을 위한 창구. `NodeViewAccess`만 쓴다 — 뷰가 backend와 무관한
		//   두 워드로 자기를 표현하려면 그 두 워드를 여기서 얻어야 한다.
		[[nodiscard]] const void* RymlTreeDuringTransition() const noexcept { return m_tree; }
		[[nodiscard]] std::size_t RymlIdDuringTransition() const noexcept { return m_id; }

		[[nodiscard]] bool IsRymlBacked() const noexcept
		{
			return Backend::Ryml == m_backend || Backend::RymlKey == m_backend;
		}

	private:
		enum class Backend : std::uint8_t { None, YamlCpp, Ryml, RymlKey };

		// ryml에서 맵의 키는 **별도 노드가 아니라 자식 노드의 속성**이다. 그래서
		// 키를 노드처럼 다루려면 같은 id를 가리키되 `Scalar()`가 키를 돌려주는
		// 모드가 필요하다. yaml-cpp는 키가 진짜 노드라 이 구분이 없다 — 어댑터가
		// 흡수하는 backend 비대칭 중 하나다.
		[[nodiscard]] static ReadNode MakeKeyView(const ReadNode& value) noexcept
		{
			ReadNode key;
			if (!value.IsRymlBacked()) return key;
			key.m_backend = Backend::RymlKey;
			key.m_tree = value.m_tree;
			key.m_id = value.m_id;
			return key;
		}

		[[noreturn]] static void ThrowConversionFailure()
		{
			throw std::runtime_error("Authoring::ReadNode: 스칼라 변환 실패 (ryml backend)");
		}
		[[noreturn]] static void ThrowBackendMismatch()
		{
			throw std::runtime_error(
				"Authoring::ReadNode: ryml 노드에 yaml-cpp 전환기 창구를 불렀다 "
				"- 이 경로는 아직 옮겨지지 않았다");
		}

		friend class MapRange;
		friend class MapIterator;

		Backend m_backend{ Backend::None };
		MetaYml::Node m_node{};
		const ryml::Tree* m_tree{ nullptr };
		std::size_t m_id{ ryml::NONE };
	};

	// ── 맵 순회 ──────────────────────────────────────────────────────────────
	//
	// 시퀀스와 달리 인덱스로 못 돈다 — yaml-cpp의 `operator[](size_t)`는 맵에서
	// **인덱스가 아니라 키**로 해석되어 조용히 엉뚱한 값을 준다. ryml은 인덱스
	// 접근이 되지만, 두 backend가 같은 인터페이스를 내주는 편이 옮길 때 어긋날
	// 여지가 적다.
	struct MapEntry
	{
		ReadNode key;
		ReadNode value;
	};

	class MapIterator
	{
	public:
		explicit MapIterator(MetaYml::const_iterator it) noexcept : m_it(it) {}
		MapIterator(const ReadNode* owner, std::size_t index) noexcept
			: m_owner(owner), m_index(index) {}

		[[nodiscard]] MapEntry operator*() const;
		MapIterator& operator++()
		{
			if (nullptr != m_owner) ++m_index; else ++m_it;
			return *this;
		}
		[[nodiscard]] bool operator!=(const MapIterator& other) const
		{
			if (nullptr != m_owner) return m_index != other.m_index;
			return m_it != other.m_it;
		}

	private:
		const ReadNode* m_owner{ nullptr };
		std::size_t m_index{ 0 };
		MetaYml::const_iterator m_it{};
	};

	class MapRange
	{
	public:
		// ★ 노드를 **값으로** 소유한다. 참조로 잡으면 `for (auto e : node.Map())`
		//   에서 임시가 먼저 죽어 반복자가 dangling이 된다.
		explicit MapRange(ReadNode node) noexcept : m_node(std::move(node)) {}

		[[nodiscard]] MapIterator begin() const
		{
			if (m_node.IsRymlBacked()) return MapIterator{ &m_node, 0 };
			return MapIterator{ m_node.m_node.begin() };
		}
		[[nodiscard]] MapIterator end() const
		{
			if (m_node.IsRymlBacked()) return MapIterator{ &m_node, m_node.Size() };
			return MapIterator{ m_node.m_node.end() };
		}

	private:
		ReadNode m_node;
	};

	inline MapEntry MapIterator::operator*() const
	{
		if (nullptr != m_owner)
		{
			const ReadNode value = m_owner->At(m_index);
			return MapEntry{ ReadNode::MakeKeyView(value), value };
		}
		return MapEntry{ ReadNode{ m_it->first }, ReadNode{ m_it->second } };
	}

	inline MapRange ReadNode::Map() const
	{
		return MapRange{ *this };
	}
}
