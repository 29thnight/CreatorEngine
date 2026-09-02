#pragma once

#include "AuthoringScalarConvert.h"

#include <c4/yml/emit.hpp>
#include <ryml/ryml.hpp>
#include <ryml/ryml_std.hpp>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

// SerializationPlan D3-b-4 — 저작 읽기 경계의 단일 ryml backend.
//
// ReadNode는 Tree를 소유하지 않는다. ParsedDocument, WriteDocument 또는 Document가
// 살아 있는 동안에만 쓸 수 있는 값 뷰다. backend 노드 타입을 소비자에게 노출하지
// 않고 실제 읽기 연산만 제공한다.
namespace Authoring
{
	class ReadNode
	{
	public:
		ReadNode() = default;

		[[nodiscard]] static ReadNode FromRyml(
			const ryml::Tree& tree, ryml::id_type id) noexcept
		{
			ReadNode node;
			if (ryml::NONE == id) return node;
			node.m_kind = Kind::Value;
			node.m_tree = &tree;
			node.m_id = id;
			return node;
		}

		[[nodiscard]] static ReadNode FromRyml(const ryml::Tree& tree) noexcept
		{
			const ryml::id_type root = tree.root_id_maybe();
			return ryml::NONE == root ? ReadNode{} : FromRyml(tree, root);
		}

		[[nodiscard]] explicit operator bool() const noexcept
		{
			return Kind::None != m_kind && nullptr != m_tree && ryml::NONE != m_id;
		}

		[[nodiscard]] bool IsNull() const
		{
			return Kind::Value == m_kind && static_cast<bool>(*this)
				&& m_tree->has_val(m_id) && m_tree->val_is_null(m_id);
		}

		[[nodiscard]] bool IsScalar() const
		{
			if (!static_cast<bool>(*this)) return false;
			if (Kind::Key == m_kind) return m_tree->has_key(m_id);
			return m_tree->has_val(m_id) && !m_tree->val_is_null(m_id);
		}

		[[nodiscard]] bool IsMap() const
		{
			return Kind::Value == m_kind && static_cast<bool>(*this)
				&& m_tree->is_map(m_id);
		}

		[[nodiscard]] bool IsSequence() const
		{
			return Kind::Value == m_kind && static_cast<bool>(*this)
				&& m_tree->is_seq(m_id);
		}

		[[nodiscard]] std::size_t Size() const
		{
			return Kind::Value == m_kind && static_cast<bool>(*this)
				? m_tree->num_children(m_id) : 0u;
		}

		// 없는 키는 오류가 아니라 유효하지 않은 노드다. ryml operator[]는 없는
		// 키에서 visit 오류를 내므로 반드시 find_child로 조회한다.
		[[nodiscard]] ReadNode operator[](const char* key) const
		{
			if (!IsMap() || nullptr == key) return {};
			const ryml::id_type child =
				m_tree->find_child(m_id, ryml::to_csubstr(key));
			return ryml::NONE == child ? ReadNode{} : FromRyml(*m_tree, child);
		}

		[[nodiscard]] bool HasChild(const char* key) const
		{
			return IsMap() && nullptr != key
				&& ryml::NONE != m_tree->find_child(m_id, ryml::to_csubstr(key));
		}

		[[nodiscard]] std::string_view Scalar() const
		{
			if (!static_cast<bool>(*this)) return {};
			if (Kind::Key == m_kind)
			{
				if (!m_tree->has_key(m_id)) return {};
				const ryml::csubstr key = m_tree->key(m_id);
				return { key.str, key.len };
			}
			if (!m_tree->has_val(m_id)) return {};
			if (m_tree->val_is_null(m_id)) return std::string_view{ "null" };
			const ryml::csubstr value = m_tree->val(m_id);
			return { value.str, value.len };
		}

		template<class T>
		[[nodiscard]] T As() const
		{
			T value{};
			if (Authoring::Scalar::TryConvert(Scalar(), value)) return value;
			ThrowConversionFailure();
		}

		template<class T>
		[[nodiscard]] T As(T fallback) const
		{
			T value{};
			return Authoring::Scalar::TryConvert(Scalar(), value) ? value : fallback;
		}

		[[nodiscard]] std::string AsString() const
		{
			return std::string{ Scalar() };
		}

		[[nodiscard]] std::string AsStringChecked() const
		{
			if (IsNull() || IsScalar()) return AsString();
			ThrowConversionFailure();
		}

		// map child를 emit할 때 key까지 딸려 나오지 않도록 value subtree만 임시
		// root에 복제한다. PrefabOverride::m_valueYaml은 문자열 저장 계약이라 이
		// 함수가 장기적으로도 필요하다.
		[[nodiscard]] std::string Dump() const
		{
			if (!static_cast<bool>(*this)) return {};
			if (Kind::Key == m_kind) return AsString();
			ryml::Tree subtree;
			subtree.duplicate_contents(m_tree, m_id, subtree.root_id());
			return ryml::emitrs_yaml<std::string>(subtree, subtree.root_id());
		}

		[[nodiscard]] ReadNode At(std::size_t index) const
		{
			if (Kind::Value != m_kind || !static_cast<bool>(*this)
				|| index >= m_tree->num_children(m_id))
				return {};
			return FromRyml(*m_tree, m_tree->child(m_id, index));
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

		[[nodiscard]] Iterator begin() const { return Iterator{ this, 0u }; }
		[[nodiscard]] Iterator end() const { return Iterator{ this, Size() }; }

		[[nodiscard]] class MapRange Map() const;

		// NodeViewAccess가 불투명 두 워드 뷰를 만들 때만 쓴다.
		[[nodiscard]] const void* TreeForView() const noexcept { return m_tree; }
		[[nodiscard]] std::size_t IdForView() const noexcept { return m_id; }

	private:
		enum class Kind : std::uint8_t { None, Value, Key };

		[[nodiscard]] static ReadNode MakeKeyView(const ReadNode& value) noexcept
		{
			ReadNode key;
			if (!value) return key;
			key.m_kind = Kind::Key;
			key.m_tree = value.m_tree;
			key.m_id = value.m_id;
			return key;
		}

		[[noreturn]] static void ThrowConversionFailure()
		{
			throw std::runtime_error("Authoring::ReadNode: scalar conversion failed");
		}

		friend class MapRange;
		friend class MapIterator;

		Kind m_kind{ Kind::None };
		const ryml::Tree* m_tree{ nullptr };
		ryml::id_type m_id{ ryml::NONE };
	};

	struct MapEntry
	{
		ReadNode key;
		ReadNode value;
	};

	class MapIterator
	{
	public:
		MapIterator(const ReadNode* owner, std::size_t index) noexcept
			: m_owner(owner), m_index(index) {}

		[[nodiscard]] MapEntry operator*() const
		{
			const ReadNode value = m_owner->At(m_index);
			return MapEntry{ ReadNode::MakeKeyView(value), value };
		}
		MapIterator& operator++() noexcept { ++m_index; return *this; }
		[[nodiscard]] bool operator!=(const MapIterator& other) const noexcept
		{
			return m_index != other.m_index;
		}

	private:
		const ReadNode* m_owner;
		std::size_t m_index;
	};

	class MapRange
	{
	public:
		// ReadNode를 값으로 잡아 `for (auto entry : node.Map())`의 임시 수명을
		// range 끝까지 유지한다. Tree 소유 수명은 호출자가 별도로 지켜야 한다.
		explicit MapRange(ReadNode node) noexcept : m_node(std::move(node)) {}

		[[nodiscard]] MapIterator begin() const { return { &m_node, 0u }; }
		[[nodiscard]] MapIterator end() const { return { &m_node, m_node.Size() }; }

	private:
		ReadNode m_node;
	};

	inline MapRange ReadNode::Map() const
	{
		return MapRange{ *this };
	}
}
