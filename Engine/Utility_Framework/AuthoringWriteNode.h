#pragma once

#include "AuthoringReadNode.h"
#include "AuthoringParseTelemetry.h"
#include "AuthoringRymlErrorPolicy.h"

#include <c4/yml/emit.hpp>
#include <ryml/ryml.hpp>
#include <ryml/ryml_std.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

// SerializationPlan D3-b-3 — 저작 writer의 ryml 경계.
//
// `WriteDocument`가 Tree를 소유하고 `WriteNode`는 그 안을 잠깐 가리킨다. NodeRef를
// 외부에 직접 노출하지 않는 이유는 읽기 쪽 `ParsedDocument`와 같다: NodeRef는 Tree를
// 소유하지 않으므로 소유자보다 오래 살면 곧바로 dangling이 된다.
//
// 키와 문자열은 반드시 Tree arena에 복사한다. ryml의 `set_key()`/`set_val()`은 받은
// 문자열을 소유하지 않으므로, reflect()가 돌려준 string_view나 지역 std::string을
// 그대로 넘기면 emit 시점에 이미 죽어 있을 수 있다.
namespace Authoring
{
	class WriteNode
	{
	public:
		WriteNode() = default;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return nullptr != m_tree && ryml::NONE != m_id;
		}

		explicit operator bool() const noexcept { return IsValid(); }

		void SetMap(bool flow = false) const
		{
			if (!IsValid()) return;
			auto node = Ref();
			const ryml::NodeType style = flow
				? ryml::NodeType{ ryml::FLOW_SL | ryml::FLOW_SPC }
				: ryml::NodeType{ ryml::BLOCK };
			if (m_tree->is_map(m_id))
				node.set_container_style(style.m_bits);
			else if (m_tree->is_seq(m_id) || m_tree->has_val(m_id))
				node.change_type(ryml::NodeType{ ryml::MAP | style.m_bits });
			else
				node.set_map(style);
		}

		void SetSequence(bool flow = false) const
		{
			if (!IsValid()) return;
			auto node = Ref();
			const ryml::NodeType style = flow
				? ryml::NodeType{ ryml::FLOW_SL | ryml::FLOW_SPC }
				: ryml::NodeType{ ryml::BLOCK };
			if (m_tree->is_seq(m_id))
				node.set_container_style(style.m_bits);
			else if (m_tree->is_map(m_id) || m_tree->has_val(m_id))
				node.change_type(ryml::NodeType{ ryml::SEQ | style.m_bits });
			else
				node.set_seq(style);
		}

		void SetFlow() const
		{
			if (!IsValid()) return;
			Ref().set_container_style(
				ryml::NodeType{ ryml::FLOW_SL | ryml::FLOW_SPC });
		}

		void SetNull() const
		{
			if (!IsValid()) return;
			// yaml-cpp emitter의 canonical null 표기는 `~`다. ryml의 VALNIL은
			// `key:`처럼 빈 값을 내므로, null로도 판정되는 plain `~`를 명시한다.
			SetScalar(std::string_view{ "~" });
		}

		void SetScalar(std::string_view value) const
		{
			if (!IsValid()) return;
			auto node = Ref();
			if (m_tree->is_map(m_id) || m_tree->is_seq(m_id))
				node.change_type(ryml::NodeType{ ryml::VAL });
			const ryml::csubstr source(value.data(), value.size());
			if (value.empty())
			{
				// yaml-cpp는 빈 문자열을 `""`로 고정한다. ryml 기본은 `''`라
				// reflection golden 바이트 계약이 흔들리므로 double quote를 강제한다.
				node.set_val(m_tree->copy_to_arena(source),
					ryml::NodeType{ ryml::VAL_DQUO });
			}
			else
			{
				node.set_val(m_tree->copy_to_arena(source));
			}
		}

		void SetScalar(const std::string& value) const
		{
			SetScalar(std::string_view{ value });
		}

		void SetScalar(const char* value) const
		{
			if (nullptr == value)
			{
				SetNull();
				return;
			}
			SetScalar(std::string_view{ value });
		}

		// ryml의 기본 bool scalar_serialize는 1/0을 낸다. yaml-cpp의 기존
		// `as<bool>()` 계약은 그 표기를 거부하고 true/false만 받으므로 writer에서
		// 명시 고정한다. D3-b-2b 스칼라 파리티가 이미 밝힌 방향 역전과 같은 축이다.
		void SetScalar(bool value) const
		{
			SetScalar(value ? std::string_view{ "true" } : std::string_view{ "false" });
		}

		template<class T>
			requires (!std::is_convertible_v<const T&, std::string_view>)
		void SetScalar(const T& value) const
		{
			if (!IsValid()) return;
			Ref().save(value);
		}

		[[nodiscard]] WriteNode Child(std::string_view key) const
		{
			if (!IsValid()) return {};
			// 이미 flow map인 노드에서 자식을 추가할 때 BLOCK으로 되돌리면
			// `key: {x: 0, y: 0}`이 두 줄로 갈라진다. 컨테이너가 아직 map이
			// 아닐 때만 기본 스타일을 부여해 호출자가 정한 스타일을 보존한다.
			if (!m_tree->is_map(m_id)) SetMap();

			const ryml::csubstr lookup(key.data(), key.size());
			ryml::id_type child = m_tree->find_child(m_id, lookup);
			if (ryml::NONE == child)
			{
				child = m_tree->append_child(m_id);
				m_tree->set_key(child, m_tree->copy_to_arena(lookup));
			}
			return WriteNode{ *m_tree, child };
		}

		[[nodiscard]] WriteNode Append() const
		{
			if (!IsValid()) return {};
			// Child()와 같은 이유로 기존 flow sequence 스타일을 보존한다.
			if (!m_tree->is_seq(m_id)) SetSequence();
			return WriteNode{ *m_tree, m_tree->append_child(m_id) };
		}

		[[nodiscard]] WriteNode At(std::size_t index) const
		{
			if (!IsValid() || !m_tree->is_seq(m_id)) return {};
			ryml::id_type child = m_tree->first_child(m_id);
			for (std::size_t current = 0;
				ryml::NONE != child && current < index; ++current)
			{
				child = m_tree->next_sibling(child);
			}
			return ryml::NONE == child ? WriteNode{} : WriteNode{ *m_tree, child };
		}

		[[nodiscard]] std::size_t Size() const noexcept
		{
			return IsValid() ? m_tree->num_children(m_id) : 0u;
		}

		bool RemoveChild(std::string_view key) const
		{
			if (!IsValid() || !m_tree->is_map(m_id)) return false;
			const ryml::csubstr lookup(key.data(), key.size());
			const ryml::id_type child = m_tree->find_child(m_id, lookup);
			if (ryml::NONE == child) return false;
			m_tree->remove(child);
			return true;
		}

		// 다른 writer 트리의 값을 이 노드에 복제한다. 대상 노드의 key는 유지하고
		// value/type/children/style만 교체한다. 중첩 런타임 타입 직렬화에서 쓰인다.
		void Assign(const WriteNode& source) const
		{
			if (!IsValid() || !source.IsValid()) return;

			auto target = Ref();
			target.clear_children();
			if (m_tree->has_val(m_id)) target.clear_val();
			target.clear_style();

			// rapidyaml's cross-tree duplicate APIs copy NodeData, including the
			// csubstr pointers into the source arena.  They therefore are not an
			// ownership transfer: once a staging document dies, duplicated keys and
			// values dangle.  Clone every scalar into the destination arena instead.
			const auto cloneContents = [&]<class Self>(Self&& self,
				const ryml::Tree& sourceTree, ryml::id_type sourceId,
				ryml::Tree& destinationTree, ryml::id_type destinationId,
				bool copyKey) -> void
			{
				const ryml::NodeType sourceType = sourceTree.type(sourceId);
				if (copyKey && sourceTree.has_key(sourceId))
				{
					destinationTree.set_key(destinationId,
						destinationTree.copy_to_arena(sourceTree.key(sourceId)),
						sourceTree.key_style(sourceId));
				}

				const ryml::NodeType containerStyle = sourceType.is_flow()
					? ryml::NodeType{ sourceType.m_bits & ryml::CONTAINER_STYLE }
					: ryml::NodeType{ ryml::BLOCK };
				if (sourceType.is_map())
					destinationTree.set_map(destinationId, containerStyle);
				else if (sourceType.is_seq())
					destinationTree.set_seq(destinationId, containerStyle);
				else if (sourceTree.has_val(sourceId))
					destinationTree.set_val(destinationId,
						destinationTree.copy_to_arena(sourceTree.val(sourceId)),
						sourceTree.val_style(sourceId));
				else
					destinationTree.set_val(destinationId, ryml::csubstr{});

				for (ryml::id_type child = sourceTree.first_child(sourceId);
					ryml::NONE != child;
					child = sourceTree.next_sibling(child))
				{
					const ryml::id_type destinationChild =
						destinationTree.append_child(destinationId);
					self(self, sourceTree, child, destinationTree,
						destinationChild, true);
				}
			};

			cloneContents(cloneContents, *source.m_tree, source.m_id,
				*m_tree, m_id, false);
		}

		// 읽기 어댑터의 value subtree를 backend와 무관하게 복제한다. Prefab 갱신처럼
		// 기존 문서에서 일부 프로퍼티만 writer 문서에 패치하는 경로가 Dump -> parse
		// 텍스트 왕복에 의존하지 않도록 하는 정식 경계다.
		void Assign(const ReadNode& source) const
		{
			if (!IsValid() || !source) return;

			auto target = Ref();
			target.clear_children();
			if (m_tree->has_val(m_id)) target.clear_val();
			target.clear_style();

			if (source.IsNull())
			{
				SetNull();
				return;
			}
			if (source.IsScalar())
			{
				SetScalar(source.Scalar());
				return;
			}
			if (source.IsSequence())
			{
				SetSequence();
				for (std::size_t index = 0; index < source.Size(); ++index)
					Append().Assign(source.At(index));
				return;
			}
			if (source.IsMap())
			{
				SetMap();
				for (const auto entry : source.Map())
					Child(entry.key.AsStringChecked()).Assign(entry.value);
				return;
			}

			// 유효하지만 타입이 없는 노드는 빈 값으로 유지한다.
			target.set_val(ryml::csubstr{});
		}

		[[nodiscard]] ReadNode Read() const
		{
			if (!IsValid()) return {};
			return ReadNode::FromRyml(*m_tree, m_id);
		}

		[[nodiscard]] std::string Dump() const
		{
			if (!IsValid()) return {};
			ryml::Tree subtree;
			subtree.duplicate_contents(m_tree, m_id, subtree.root_id());
			return ryml::emitrs_yaml<std::string>(subtree, subtree.root_id());
		}

	private:
		friend class WriteDocument;
		friend struct MutableNodeViewAccess;

		WriteNode(ryml::Tree& tree, ryml::id_type id) noexcept
			: m_tree(&tree), m_id(id)
		{
		}

		[[nodiscard]] ryml::NodeRef Ref() const
		{
			return ryml::NodeRef{ m_tree, m_id };
		}

		ryml::Tree* m_tree{ nullptr };
		ryml::id_type m_id{ ryml::NONE };
	};

	class WriteDocument
	{
	public:
		WriteDocument()
		{
			EnsureRymlErrorPolicy();
		}

		WriteDocument(WriteDocument&&) noexcept = default;
		WriteDocument& operator=(WriteDocument&&) noexcept = default;
		WriteDocument(const WriteDocument&) = delete;
		WriteDocument& operator=(const WriteDocument&) = delete;

		[[nodiscard]] static std::optional<WriteDocument> ParseText(
			std::string_view text, std::string* error = nullptr)
		{
			EnsureRymlErrorPolicy();
			try
			{
				WriteDocument document;
				RecordTextParserCall("<write-memory>");
				document.m_tree = ryml::parse_in_arena(
					ryml::csubstr(text.data(), text.size()));
				if (error) error->clear();
				return document;
			}
			catch (const std::exception& exception)
			{
				if (error) *error = exception.what();
				return std::nullopt;
			}
		}

		[[nodiscard]] static std::optional<WriteDocument> ParseFile(
			const std::filesystem::path& path, std::string* error = nullptr)
		{
			std::ifstream input(path, std::ios::binary);
			if (!input.is_open())
			{
				if (error) *error = "failed to open authoring document: "
					+ path.string();
				return std::nullopt;
			}
			const std::string text{ std::istreambuf_iterator<char>{ input },
				std::istreambuf_iterator<char>{} };
			return ParseText(text, error);
		}

		[[nodiscard]] WriteNode Root()
		{
			return WriteNode{ m_tree, m_tree.root_id() };
		}

		[[nodiscard]] ReadNode Root() const
		{
			return ReadNode::FromRyml(m_tree);
		}

		[[nodiscard]] std::string Dump() const
		{
			// WriteDocument는 단일 root value를 소유한다. stream/document wrapper를
			// 암묵 추론시키지 않고 그 root를 명시해 WriteNode::Dump와 맞춘다.
			return ryml::emitrs_yaml<std::string>(m_tree, m_tree.root_id());
		}

		[[nodiscard]] bool IsEmpty() const noexcept
		{
			const ryml::id_type root = m_tree.root_id();
			return m_tree.type(root).is_notype();
		}

		void Clear() noexcept
		{
			m_tree = ryml::Tree{};
		}

	private:
		friend class ParsedDocument;
		ryml::Tree m_tree{};
	};
}
