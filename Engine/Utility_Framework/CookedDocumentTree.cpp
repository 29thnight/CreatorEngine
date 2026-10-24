#include "CookedDocumentTree.h"

namespace Cedo
{
    namespace
    {
        class Reader
        {
        public:
            explicit Reader(std::span<const std::byte> input) : input_(input) {}

            std::size_t Remaining() const noexcept { return input_.size() - position_; }

            std::uint32_t Integer(std::size_t bytes)
            {
                if (bytes > Remaining()) throw std::runtime_error("Truncated CEDO integer");
                std::uint32_t value = 0;
                for (std::size_t i = 0; i < bytes; ++i)
                    value |= std::to_integer<std::uint32_t>(input_[position_++]) << (i * 8);
                return value;
            }

            std::string String(std::uint32_t bytes)
            {
                if (bytes > max_scalar_bytes || bytes > Remaining())
                    throw std::runtime_error("Invalid CEDO string length");
                const auto* start = reinterpret_cast<const char*>(input_.data() + position_);
                position_ += bytes;
                return {start, bytes};
            }

            Node ReadNode(std::uint32_t depth, bool mapChild, std::uint32_t& count,
                          std::uint32_t expected)
            {
                if (depth > max_depth || count >= expected)
                    throw std::runtime_error("CEDO depth or node count exceeded");

                Node node;
                node.kind = static_cast<Kind>(Integer(1));
                const auto flags = Integer(1);
                const auto reserved = Integer(2);
                const auto keyBytes = Integer(4);
                const auto valueBytes = Integer(4);
                const auto children = Integer(4);

                if (flags || reserved || (mapChild != (keyBytes != 0)) || children > expected - count - 1)
                    throw std::runtime_error("Invalid CEDO node header");

                node.key = String(keyBytes);
                node.value = String(valueBytes);
                ++count;

                switch (node.kind)
                {
                case Kind::Null:
                    if (valueBytes || children) throw std::runtime_error("Nonempty CEDO null");
                    break;
                case Kind::Scalar:
                    if (children) throw std::runtime_error("CEDO scalar has children");
                    break;
                case Kind::Map:
                case Kind::Sequence:
                    if (valueBytes) throw std::runtime_error("CEDO container has a scalar payload");
                    break;
                default:
                    throw std::runtime_error("Unknown CEDO node kind");
                }

                if (children > Remaining() / 16)
                    throw std::runtime_error("Truncated CEDO children");
                node.children.reserve(children);
                std::unordered_set<std::string> keys;
                for (std::uint32_t i = 0; i < children; ++i)
                {
                    auto child = ReadNode(depth + 1, node.kind == Kind::Map, count, expected);
                    if (node.kind == Kind::Map && !keys.insert(child.key).second)
                        throw std::runtime_error("Duplicate CEDO map key");
                    node.children.push_back(std::move(child));
                }
                return node;
            }

        private:
            std::span<const std::byte> input_;
            std::size_t position_ = 0;
        };
    }

    std::optional<Document> Decode(std::span<const std::byte> bytes, std::string& error)
    {
        error.clear();
        try
        {
            if (bytes.size() < header_bytes || bytes.size() > max_document_bytes
                || std::memcmp(bytes.data(), "CEDO", 4) != 0)
                throw std::runtime_error("Expected a bounded CEDO document; authoring text is not accepted");

            Reader reader(bytes.subspan(4));
            const auto format = reader.Integer(2);
            const auto reserved = reader.Integer(2);
            const auto nodes = reader.Integer(4);
            const auto payload = reader.Integer(4);
            if (format != version || reserved || !nodes || nodes > max_nodes
                || payload != reader.Remaining() || nodes > payload / 16)
                throw std::runtime_error("Invalid CEDO header or version; recook the source");

            Document result;
            result.root = reader.ReadNode(0, false, result.nodes, nodes);
            if (reader.Remaining() || result.nodes != nodes)
                throw std::runtime_error("CEDO node count or trailing bytes mismatch");
            return result;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return std::nullopt;
        }
    }
}
