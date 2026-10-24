#include "AuthoringCookedDocument.h"
#include "AuthoringBase64.h"
#include "CookedDocumentTree.h"

#include "StandardLibrary.h"

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

        void CopyCookedNode(const Cedo::Node& source, const WriteNode& target)
        {
            switch (source.kind)
            {
            case Cedo::Kind::Null: target.SetNull(); break;
            case Cedo::Kind::Scalar: target.SetString(source.value); break;
            case Cedo::Kind::Map: target.SetMap(); break;
            case Cedo::Kind::Sequence: target.SetSequence(); break;
            }
            for (const auto& child : source.children)
            {
                const auto destination = source.kind == Cedo::Kind::Map
                    ? target.Child(child.key) : target.Append();
                CopyCookedNode(child, destination);
            }
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
        auto decoded = Cedo::Decode(bytes, error);
        if (!decoded) return std::nullopt;
        WriteDocument document;
        CopyCookedNode(decoded->root, document.Root());
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
