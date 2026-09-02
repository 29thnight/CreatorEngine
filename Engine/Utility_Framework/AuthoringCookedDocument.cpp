#include "AuthoringCookedDocument.h"
#include "AuthoringBase64.h"

#include <algorithm>
#include <limits>
#include <string_view>
#include <unordered_set>

namespace Authoring
{
	namespace
	{
		constexpr std::size_t kHeaderBytes = 16u;
		constexpr std::size_t kNodeHeaderBytes = 16u;
		constexpr std::size_t kMaxDocumentBytes = 64u * 1024u * 1024u;
		constexpr std::uint32_t kMaxNodeCount = 1'000'000u;
		constexpr std::uint32_t kMaxScalarBytes = 16u * 1024u * 1024u;
		constexpr std::uint32_t kMaxDepth = 512u;

		enum class NodeKind : std::uint8_t
		{
			Null = 1u,
			Scalar = 2u,
			Map = 3u,
			Sequence = 4u,
		};

		void WriteU8(std::vector<std::byte>& bytes, std::uint8_t value)
		{
			bytes.push_back(static_cast<std::byte>(value));
		}

		void WriteU16(std::vector<std::byte>& bytes, std::uint16_t value)
		{
			WriteU8(bytes, static_cast<std::uint8_t>(value));
			WriteU8(bytes, static_cast<std::uint8_t>(value >> 8u));
		}

		void WriteU32(std::vector<std::byte>& bytes, std::uint32_t value)
		{
			for (std::uint32_t shift = 0u; shift < 32u; shift += 8u)
				WriteU8(bytes, static_cast<std::uint8_t>(value >> shift));
		}

		bool AppendString(std::vector<std::byte>& bytes, std::string_view value,
			std::string& error)
		{
			if (value.size() > kMaxScalarBytes
				|| value.size() > std::numeric_limits<std::uint32_t>::max())
			{
				error = "cooked document scalar가 크기 상한을 넘었다";
				return false;
			}
			if (bytes.size() > kMaxDocumentBytes - value.size())
			{
				error = "cooked document가 64MiB 상한을 넘었다";
				return false;
			}
			const auto* begin = reinterpret_cast<const std::byte*>(value.data());
			bytes.insert(bytes.end(), begin, begin + value.size());
			return true;
		}

		bool EncodeNode(const ReadNode& node, std::string_view key,
			std::uint32_t depth, std::uint32_t& nodeCount,
			std::vector<std::byte>& payload, std::string& error)
		{
			if (!node)
			{
				error = "유효하지 않은 node를 cooked document로 쓸 수 없다";
				return false;
			}
			if (depth > kMaxDepth)
			{
				error = "cooked document node 깊이가 상한을 넘었다";
				return false;
			}
			if (nodeCount == kMaxNodeCount)
			{
				error = "cooked document node 수가 상한을 넘었다";
				return false;
			}
			++nodeCount;

			NodeKind kind{};
			std::string_view scalar;
			if (node.IsNull())
				kind = NodeKind::Null;
			else if (node.IsScalar())
			{
				kind = NodeKind::Scalar;
				scalar = node.Scalar();
			}
			else if (node.IsMap())
				kind = NodeKind::Map;
			else if (node.IsSequence())
				kind = NodeKind::Sequence;
			else
			{
				error = "지원하지 않는 authoring node type이다";
				return false;
			}

			if (key.size() > kMaxScalarBytes || scalar.size() > kMaxScalarBytes
				|| node.Size() > std::numeric_limits<std::uint32_t>::max())
			{
				error = "cooked document node 필드가 크기 상한을 넘었다";
				return false;
			}
			if (payload.size() > kMaxDocumentBytes - kNodeHeaderBytes)
			{
				error = "cooked document가 64MiB 상한을 넘었다";
				return false;
			}

			WriteU8(payload, static_cast<std::uint8_t>(kind));
			WriteU8(payload, 0u);
			WriteU16(payload, 0u);
			WriteU32(payload, static_cast<std::uint32_t>(key.size()));
			WriteU32(payload, static_cast<std::uint32_t>(scalar.size()));
			WriteU32(payload, static_cast<std::uint32_t>(node.Size()));
			if (!AppendString(payload, key, error)
				|| !AppendString(payload, scalar, error)) return false;

			if (node.IsMap())
			{
				std::unordered_set<std::string> keys;
				keys.reserve(node.Size());
				for (const MapEntry entry : node.Map())
				{
					const std::string childKey = entry.key.AsStringChecked();
					if (!keys.insert(childKey).second)
					{
						error = "cooked document map에 중복 key가 있다: " + childKey;
						return false;
					}
					if (!EncodeNode(entry.value, childKey, depth + 1u,
						nodeCount, payload, error)) return false;
				}
			}
			else if (node.IsSequence())
			{
				for (const ReadNode child : node)
				{
					if (!EncodeNode(child, {}, depth + 1u,
						nodeCount, payload, error)) return false;
				}
			}
			return true;
		}

		class Reader final
		{
		public:
			explicit Reader(std::span<const std::byte> bytes) noexcept
				: bytes_(bytes) {}

			[[nodiscard]] std::size_t Remaining() const noexcept
			{
				return bytes_.size() - offset_;
			}

			[[nodiscard]] std::uint8_t U8(bool& ok) noexcept
			{
				if (Remaining() < 1u) { ok = false; return 0u; }
				return std::to_integer<std::uint8_t>(bytes_[offset_++]);
			}

			[[nodiscard]] std::uint16_t U16(bool& ok) noexcept
			{
				const std::uint16_t a = U8(ok);
				const std::uint16_t b = U8(ok);
				return static_cast<std::uint16_t>(a | (b << 8u));
			}

			[[nodiscard]] std::uint32_t U32(bool& ok) noexcept
			{
				std::uint32_t value{};
				for (std::uint32_t shift = 0u; shift < 32u; shift += 8u)
					value |= static_cast<std::uint32_t>(U8(ok)) << shift;
				return value;
			}

			[[nodiscard]] std::string_view String(std::uint32_t length,
				bool& ok) noexcept
			{
				if (length > Remaining()) { ok = false; return {}; }
				const char* data = reinterpret_cast<const char*>(bytes_.data() + offset_);
				offset_ += length;
				return { data, length };
			}

		private:
			std::span<const std::byte> bytes_{};
			std::size_t offset_{};
		};

		enum class Relation { Root, MapChild, SequenceChild };

		bool DecodeNode(Reader& reader, const WriteNode& parent,
			const WriteNode& root, Relation relation,
			std::unordered_set<std::string>* siblingKeys,
			std::uint32_t depth, std::uint32_t& decodedNodes,
			std::string& error)
		{
			if (depth > kMaxDepth || decodedNodes == kMaxNodeCount)
			{
				error = "cooked document node 상한을 넘었다";
				return false;
			}
			bool ok = true;
			const auto kind = static_cast<NodeKind>(reader.U8(ok));
			const std::uint8_t flags = reader.U8(ok);
			const std::uint16_t reserved = reader.U16(ok);
			const std::uint32_t keyLength = reader.U32(ok);
			const std::uint32_t valueLength = reader.U32(ok);
			const std::uint32_t childCount = reader.U32(ok);
			if (!ok)
			{
				error = "cooked document node header가 잘렸다";
				return false;
			}
			if (flags != 0u || reserved != 0u
				|| keyLength > kMaxScalarBytes || valueLength > kMaxScalarBytes
				|| childCount > kMaxNodeCount)
			{
				error = "cooked document node header가 유효하지 않다";
				return false;
			}

			const std::string_view key = reader.String(keyLength, ok);
			const std::string_view value = reader.String(valueLength, ok);
			if (!ok)
			{
				error = "cooked document node payload가 잘렸다";
				return false;
			}
			if ((relation == Relation::MapChild) != (keyLength != 0u)
				|| (relation != Relation::MapChild && keyLength != 0u))
			{
				error = "cooked document map key 배치가 유효하지 않다";
				return false;
			}

			WriteNode target;
			if (relation == Relation::Root)
				target = root;
			else if (relation == Relation::SequenceChild)
				target = parent.Append();
			else
			{
				if (nullptr == siblingKeys
					|| !siblingKeys->insert(std::string{ key }).second)
				{
					error = "cooked document map key가 중복됐다";
					return false;
				}
				target = parent.Child(key);
			}
			if (!target)
			{
				error = "cooked document node를 만들 수 없다";
				return false;
			}
			++decodedNodes;

			switch (kind)
			{
			case NodeKind::Null:
				if (valueLength != 0u || childCount != 0u)
				{
					error = "null cooked node에 값 또는 자식이 있다";
					return false;
				}
				target.SetNull();
				break;
			case NodeKind::Scalar:
				if (childCount != 0u)
				{
					error = "scalar cooked node에 자식이 있다";
					return false;
				}
				target.SetScalar(value);
				break;
			case NodeKind::Map:
			case NodeKind::Sequence:
				if (valueLength != 0u)
				{
					error = "container cooked node에 scalar 값이 있다";
					return false;
				}
				if (kind == NodeKind::Map) target.SetMap();
				else target.SetSequence();
				break;
			default:
				error = "cooked document node kind가 유효하지 않다";
				return false;
			}

			std::unordered_set<std::string> keys;
			if (kind == NodeKind::Map) keys.reserve(childCount);
			for (std::uint32_t index = 0u; index < childCount; ++index)
			{
				const Relation childRelation = kind == NodeKind::Map
					? Relation::MapChild : Relation::SequenceChild;
				if (kind != NodeKind::Map && kind != NodeKind::Sequence)
				{
					error = "scalar cooked node childCount가 0이 아니다";
					return false;
				}
				if (!DecodeNode(reader, target, root, childRelation,
					kind == NodeKind::Map ? &keys : nullptr,
					depth + 1u, decodedNodes, error)) return false;
			}
			return true;
		}
	}

	bool IsCookedDocument(std::span<const std::byte> bytes) noexcept
	{
		return bytes.size() >= kCookedDocumentMagic.size()
			&& std::equal(kCookedDocumentMagic.begin(),
				kCookedDocumentMagic.end(), bytes.begin());
	}

	bool EncodeCookedDocument(const ReadNode& root,
		std::vector<std::byte>& outBytes, std::string& error)
	{
		outBytes.clear();
		error.clear();
		std::vector<std::byte> payload;
		std::uint32_t nodeCount{};
		if (!EncodeNode(root, {}, 0u, nodeCount, payload, error)) return false;
		if (payload.size() > std::numeric_limits<std::uint32_t>::max()
			|| payload.size() > kMaxDocumentBytes - kHeaderBytes)
		{
			error = "cooked document payload가 크기 상한을 넘었다";
			return false;
		}

		outBytes.reserve(kHeaderBytes + payload.size());
		outBytes.insert(outBytes.end(), kCookedDocumentMagic.begin(),
			kCookedDocumentMagic.end());
		WriteU16(outBytes, kCookedDocumentVersion);
		WriteU16(outBytes, 0u);
		WriteU32(outBytes, nodeCount);
		WriteU32(outBytes, static_cast<std::uint32_t>(payload.size()));
		outBytes.insert(outBytes.end(), payload.begin(), payload.end());
		return true;
	}

	std::optional<WriteDocument> DecodeCookedDocument(
		std::span<const std::byte> bytes, std::string& error)
	{
		error.clear();
		if (bytes.size() < kHeaderBytes || bytes.size() > kMaxDocumentBytes
			|| !IsCookedDocument(bytes))
		{
			error = "CEDO magic 또는 문서 크기가 유효하지 않다";
			return std::nullopt;
		}
		Reader reader(bytes.subspan(kCookedDocumentMagic.size()));
		bool ok = true;
		const std::uint16_t version = reader.U16(ok);
		const std::uint16_t reserved = reader.U16(ok);
		const std::uint32_t expectedNodes = reader.U32(ok);
		const std::uint32_t payloadBytes = reader.U32(ok);
		if (!ok || version != kCookedDocumentVersion || reserved != 0u
			|| expectedNodes == 0u || expectedNodes > kMaxNodeCount
			|| payloadBytes != reader.Remaining())
		{
			error = "CEDO header/version이 유효하지 않다; 재쿠킹이 필요하다";
			return std::nullopt;
		}

		WriteDocument document;
		std::uint32_t decodedNodes{};
		if (!DecodeNode(reader, {}, document.Root(), Relation::Root, nullptr,
			0u, decodedNodes, error)) return std::nullopt;
		if (reader.Remaining() != 0u || decodedNodes != expectedNodes)
		{
			error = "CEDO node 수 또는 trailing byte가 유효하지 않다";
			return std::nullopt;
		}
		return document;
	}

	bool EncodeCookedDocumentTextEnvelope(const ReadNode& root,
		std::string& outText, std::string& error)
	{
		std::vector<std::byte> bytes;
		if (!EncodeCookedDocument(root, bytes, error))
		{
			outText.clear();
			return false;
		}
		outText.assign(kCookedDocumentTextEnvelopePrefix);
		outText += Base64::Encode(
			reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
		return true;
	}

	std::optional<WriteDocument> DecodeCookedDocumentTextEnvelope(
		std::string_view text, std::string& error)
	{
		error.clear();
		if (!text.starts_with(kCookedDocumentTextEnvelopePrefix))
		{
			error = "CEDO1 text envelope prefix가 없다";
			return std::nullopt;
		}
		std::vector<std::uint8_t> decoded;
		if (!Base64::Decode(text.substr(kCookedDocumentTextEnvelopePrefix.size()),
			decoded))
		{
			error = "CEDO1 text envelope base64가 유효하지 않다";
			return std::nullopt;
		}
		const std::span<const std::byte> bytes{
			reinterpret_cast<const std::byte*>(decoded.data()), decoded.size() };
		return DecodeCookedDocument(bytes, error);
	}
}
