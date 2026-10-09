#include "CookedModelSubAssetCodec.h"
#include "../../Assets/AssetIdentityProfile.h"
#include "../Import/MeshletBuilder.h"
#include "../Import/MeshLodBuilder.h"

#include <cstring>

#include <algorithm>
#include <bit>
#include <cmath>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace experiment::cooked
{
    namespace
    {
        constexpr std::uint32_t kSkeletonMagic = 0x4c534543u; // CESL
        constexpr std::uint32_t kClipMagic = 0x4e414543u; // CEAN
        constexpr std::uint32_t kDescriptorMagic = 0x444d4543u; // CEMD
        constexpr std::size_t kMaxString = 65536u;
        constexpr std::uint32_t kMaxKeys = 4u * 1024u * 1024u;

        void Require(bool condition, const char* message)
        {
            if (!condition)
            {
                throw std::runtime_error(message);
            }
        }

        bool V8(const AssetId& value)
        {
            return value.IsValid() && (value.value.data[6] & 0xf0u) == 0x80u &&
                (value.value.data[8] & 0xc0u) == 0x80u;
        }

        struct WireWriter final
        {
            std::vector<std::byte> bytes{};

            template<class T> void Integer(T value)
            {
                static_assert(std::is_unsigned_v<T>);
                Require(bytes.size() <= kModelSubAssetMaxBytes - sizeof(T), "Payload exceeds byte budget");
                for (std::size_t index = 0; index < sizeof(T); ++index)
                {
                    bytes.push_back(static_cast<std::byte>((value >> (index * 8u)) & 0xffu));
                }
            }
            void Float(float value)
            {
                Require(std::isfinite(value), "Nonfinite float in model subasset");
                Integer(std::bit_cast<std::uint32_t>(value));
            }
            void Double(double value)
            {
                Require(std::isfinite(value), "Nonfinite time in model subasset");
                Integer(std::bit_cast<std::uint64_t>(value));
            }
            void Text(const std::string& value)
            {
                Require(value.size() <= kMaxString && value.find('\0') == std::string::npos,
                    "Invalid or oversized model subasset string");
                Integer(static_cast<std::uint32_t>(value.size()));
                for (const unsigned char ch : value)
                {
                    Integer(static_cast<std::uint8_t>(ch));
                }
            }
            template<class T> void Bytes(const T& value)
            {
                for (const auto byte : value)
                {
                    Integer(static_cast<std::uint8_t>(byte));
                }
            }
            void Matrix(const math::matrix4x4& value)
            {
                for (const auto& row : value.m)
                {
                    for (const float element : row)
                    {
                        Float(element);
                    }
                }
            }
            void Header(std::uint32_t magic, std::uint32_t version)
            {
                Integer(magic);
                Integer(version);
            }
        };

        struct WireReader final
        {
            std::span<const std::byte> bytes{};
            std::size_t offset{};
            explicit WireReader(std::span<const std::byte> source) : bytes(source)
            {
                Require(bytes.size() <= kModelSubAssetMaxBytes, "Payload exceeds byte budget");
            }
            template<class T> T Integer()
            {
                static_assert(std::is_unsigned_v<T>);
                Require(sizeof(T) <= bytes.size() - offset, "Truncated model subasset");
                T value{};
                for (std::size_t index = 0; index < sizeof(T); ++index)
                {
                    value |= static_cast<T>(std::to_integer<std::uint8_t>(bytes[offset++])) << (index * 8u);
                }
                return value;
            }
            float Float()
            {
                const auto value = std::bit_cast<float>(Integer<std::uint32_t>());
                Require(std::isfinite(value), "Nonfinite model subasset float");
                return value;
            }
            double Double()
            {
                const auto value = std::bit_cast<double>(Integer<std::uint64_t>());
                Require(std::isfinite(value), "Nonfinite model subasset time");
                return value;
            }
            std::uint32_t Count(std::uint32_t maximum, std::size_t minimumBytes)
            {
                const auto count = Integer<std::uint32_t>();
                Require(count <= maximum && count <= (bytes.size() - offset) / minimumBytes,
                    "Model subasset count exceeds budget or remaining bytes");
                return count;
            }
            std::string Text()
            {
                const auto size = Count(static_cast<std::uint32_t>(kMaxString), 1u);
                std::string value(reinterpret_cast<const char*>(bytes.data() + offset), size);
                offset += size;
                Require(value.find('\0') == std::string::npos, "NUL in model subasset string");
                return value;
            }
            template<class T> void Bytes(T& out)
            {
                for (auto& byte : out)
                {
                    byte = Integer<std::uint8_t>();
                }
            }
            math::matrix4x4 Matrix()
            {
                math::matrix4x4 value{};
                for (auto& row : value.m)
                {
                    for (float& element : row)
                    {
                        element = Float();
                    }
                }
                return value;
            }
            void Header(std::uint32_t magic, std::uint32_t version)
            {
                Require(Integer<std::uint32_t>() == magic, "Wrong model subasset payload type");
                Require(Integer<std::uint32_t>() == version, "Unsupported model subasset schema");
            }
            void End() const
            {
                Require(offset == bytes.size(), "Trailing model subasset bytes");
            }
        };

        template<class Work> bool Checked(Work&& work, std::string& failure)
        {
            try
            {
                work();
                failure.clear();
                return true;
            }
            catch (const std::exception& error)
            {
                failure = error.what();
                return false;
            }
        }

        void Layout(const Skeleton& skeleton, WireWriter& wire)
        {
            Require(!skeleton.bones.empty() && skeleton.bones.size() <= kModelSubAssetMaxBones &&
                IsInRange(skeleton.rootBone, skeleton.bones.size()), "Invalid skeleton bone count or root");
            std::set<std::string> names;
            wire.Text("ce.ordered-bone-layout.v1");
            wire.Integer(static_cast<std::uint32_t>(skeleton.bones.size()));
            wire.Integer(skeleton.rootBone.Value());
            for (std::size_t index = 0; index < skeleton.bones.size(); ++index)
            {
                const auto& bone = skeleton.bones[index];
                Require(!bone.name.empty() && names.insert(bone.name).second,
                    "Bone names must be unique and nonempty for an unambiguous ordered layout");
                Require(bone.parent.IsValid() ? bone.parent.Value() < index : index == skeleton.rootBone.Value(),
                    "Skeleton parent must precede its child; exactly one declared root is required");
                wire.Text(bone.name);
                wire.Integer(bone.parent.Value());
            }
        }

        void Timing(double duration, double rate)
        {
            Require(std::isfinite(duration) && duration >= 0.0 && std::isfinite(rate) && rate > 0.0,
                "Invalid animation duration/rate");
        }

        void Mode(InterpolationMode mode)
        {
            Require(mode == InterpolationMode::Linear || mode == InterpolationMode::Step,
                "Unsupported animation interpolation");
        }

        template<class Key> void Keys(WireWriter& wire, const std::vector<Key>& keys, double duration,
            std::uint64_t& total)
        {
            Require(keys.size() <= kMaxKeys && total + keys.size() <= kMaxKeys, "Animation key budget exceeded");
            total += keys.size();
            wire.Integer(static_cast<std::uint32_t>(keys.size()));
            double previous = -1.0;
            for (const auto& key : keys)
            {
                Require(key.time >= 0.0 && key.time <= duration && key.time > previous, "Unordered animation key time");
                wire.Double(key.time);
                previous = key.time;
                if constexpr (std::is_same_v<Key, RotationKey>)
                {
                    const auto& value = key.quaternion;
                    Require(value.x != 0.0f || value.y != 0.0f || value.z != 0.0f || value.w != 0.0f,
                        "Zero animation quaternion");
                    wire.Float(value.x); wire.Float(value.y); wire.Float(value.z); wire.Float(value.w);
                }
                else
                {
                    wire.Float(key.value.x); wire.Float(key.value.y); wire.Float(key.value.z);
                }
            }
        }

        template<class Key> void Keys(WireReader& wire, std::vector<Key>& keys, double duration,
            std::uint64_t& total)
        {
            constexpr auto width = std::is_same_v<Key, RotationKey> ? 24u : 20u;
            const auto count = wire.Count(kMaxKeys, width);
            Require(total + count <= kMaxKeys, "Animation key budget exceeded");
            total += count;
            keys.reserve(count);
            double previous = -1.0;
            for (std::uint32_t index = 0; index < count; ++index)
            {
                Key key;
                key.time = wire.Double();
                Require(key.time >= 0.0 && key.time <= duration && key.time > previous, "Unordered animation key time");
                previous = key.time;
                if constexpr (std::is_same_v<Key, RotationKey>)
                {
                    auto& value = key.quaternion;
                    value.x = wire.Float(); value.y = wire.Float(); value.z = wire.Float(); value.w = wire.Float();
                    Require(value.x != 0.0f || value.y != 0.0f || value.z != 0.0f || value.w != 0.0f,
                        "Zero animation quaternion");
                }
                else
                {
                    key.value.x = wire.Float(); key.value.y = wire.Float(); key.value.z = wire.Float();
                }
                keys.push_back(key);
            }
        }

        void ClipHeader(const AnimationClipArtifact& value)
        {
            Require(V8(value.skeletonAssetId) && value.requiredBoneCount != 0 &&
                value.requiredBoneCount <= kModelSubAssetMaxBones &&
                std::ranges::any_of(value.requiredBoneLayoutSha256, [](auto byte) { return byte != 0u; }),
                "Invalid required skeleton identity/layout");
            Require(value.clip.channels.size() <= value.requiredBoneCount, "Too many animation channels");
            Timing(value.clip.durationTicks, value.clip.ticksPerSecond);
        }

        constexpr std::uint32_t kGeometryMagic = 0x45474543u; // CEGE
        constexpr std::uint32_t kMaxGeometryVertices = kModelSubAssetMaxBytes / 48u;
        constexpr std::uint32_t kMaxGeometryIndices = kModelSubAssetMaxBytes / 4u;
        constexpr std::uint32_t kMaxDescriptorEntries = 65536u;

        void GeometryBounds(const math::aabb& value)
        {
            for (const float scalar : { value.center.x, value.center.y, value.center.z,
                value.extents.x, value.extents.y, value.extents.z })
            {
                Require(std::isfinite(scalar), "Nonfinite geometry bounds");
            }
            Require(value.extents.x >= 0.0f && value.extents.y >= 0.0f && value.extents.z >= 0.0f,
                "Invalid geometry bounds extents");
        }

        void GeometryBounds(WireWriter& wire, const math::aabb& value)
        {
            GeometryBounds(value);
            wire.Float(value.center.x); wire.Float(value.center.y); wire.Float(value.center.z);
            wire.Float(value.extents.x); wire.Float(value.extents.y); wire.Float(value.extents.z);
        }

        math::aabb GeometryBounds(WireReader& wire)
        {
            math::aabb value;
            value.center.x = wire.Float(); value.center.y = wire.Float(); value.center.z = wire.Float();
            value.extents.x = wire.Float(); value.extents.y = wire.Float(); value.extents.z = wire.Float();
            GeometryBounds(value);
            return value;
        }

        void GeometryHeader(VertexAttributeMask attributes, std::uint32_t stride,
            std::size_t vertexCount, std::size_t indexCount)
        {
            Require(VertexBuffer::IsSupportedLayout(attributes) && stride == StrideOf(attributes),
                "Unsupported geometry vertex layout/stride");
            Require(vertexCount != 0u && vertexCount <= kMaxGeometryVertices &&
                indexCount != 0u && indexCount <= kMaxGeometryIndices && indexCount % 3u == 0u,
                "Invalid or oversized geometry counts");
        }

        void GeometryBase(const ModelGeometryArtifact& value)
        {
            const auto& mesh = value.mesh;
            GeometryHeader(mesh.vertices.AttributeMask(), mesh.vertices.Stride(), mesh.vertices.size(), mesh.indices.size());
            GeometryBounds(mesh.bounds);
            const bool skinned = Has(mesh.vertices.AttributeMask(), VertexAttribute::BoneIndices);
            Require(skinned ? value.requiredBoneCount != 0u && value.requiredBoneCount <= kModelSubAssetMaxBones &&
                value.requiredSkinBindingSha256 != Sha256Digest{} :
                value.requiredBoneCount == 0u && value.requiredSkinBindingSha256 == Sha256Digest{},
                "Geometry skin layout and binding requirement disagree");
            for (const auto index : mesh.indices)
            {
                Require(index < mesh.vertices.size(), "Geometry index exceeds vertex count");
            }
            math::vector3 minimum{}, maximum{};
            for (std::size_t index = 0; index < mesh.vertices.size(); ++index)
            {
                const auto vertex = mesh.vertices[index];
                // product MeshSurface의 입력 범위와 같은 경계다. decode 성공 뒤 실제
                // 소비자에서 거부되는 정점이나 서로 다른 skinning 분기를 허용하지 않는다.
                for (const auto& attribute : kVertexAttributeTable)
                {
                    if (!Has(mesh.vertices.AttributeMask(), attribute.attribute) ||
                        attribute.format == VertexFormat::RGBA8Uint)
                    {
                        continue;
                    }
                    const auto begin = index * mesh.vertices.Stride() +
                        OffsetOf(mesh.vertices.AttributeMask(), attribute.attribute);
                    for (std::size_t component = 0; component < SizeOf(attribute.format); component += sizeof(float))
                    {
                        float scalar{};
                        std::memcpy(&scalar, mesh.vertices.Bytes().data() + begin + component, sizeof(scalar));
                        Require(std::isfinite(scalar) && std::abs(scalar) <= 1e6f,
                            "Geometry vertex attribute is outside the finite product-rendering range");
                    }
                }
                Require(std::hypot(vertex.normal.x, vertex.normal.y, vertex.normal.z) > 1e-10,
                    "Geometry vertex needs a nonzero normal");
                Require(vertex.tangent.w == -1.0f || vertex.tangent.w == 1.0f,
                    "Geometry vertex needs an authored tangent handedness sign");
                const auto& position = vertex.position;
                if (index == 0u)
                {
                    minimum = position;
                    maximum = position;
                }
                else
                {
                    minimum.x = (std::min)(minimum.x, position.x);
                    minimum.y = (std::min)(minimum.y, position.y);
                    minimum.z = (std::min)(minimum.z, position.z);
                    maximum.x = (std::max)(maximum.x, position.x);
                    maximum.y = (std::max)(maximum.y, position.y);
                    maximum.z = (std::max)(maximum.z, position.z);
                }
                for (std::size_t slot = 0; skinned && slot < MaxBoneInfluences; ++slot)
                {
                    const auto bone = vertex.boneIndices[slot];
                    const auto weight = vertex.boneWeights[slot];
                    Require(std::isfinite(weight) && weight >= 0.0f && weight <= 1.0f,
                        "Geometry bone weight must be in the range 0..1");
                    Require(bone == InvalidPackedBoneIndex ? weight == 0.0f :
                        bone <= MaxPackedBoneIndex && bone < value.requiredBoneCount,
                        "Geometry bone index must be 0..254 in the selected skeleton, or 255 unused");
                    for (std::size_t prior = 0; weight > 0.0f && prior < slot; ++prior)
                    {
                        Require(vertex.boneWeights[prior] == 0.0f || vertex.boneIndices[prior] != bone,
                            "Duplicate weighted geometry bone index");
                    }
                }
                if (skinned)
                {
                    const auto& weights = vertex.boneWeights;
                    const float sum = weights[0] + weights[1] + weights[2] + weights[3];
                    Require(sum == 0.0f || std::abs(sum - 1.0f) <= 1e-5f,
                        "Active geometry skin weights must sum to one");
                    Require(sum == 0.0f || weights[0] > 0.0f,
                        "Active geometry skin weights require a positive first slot");
                }
            }
            const auto bounds = math::aabb::from_min_max(minimum, maximum);
            Require(bounds.center.x == mesh.bounds.center.x && bounds.center.y == mesh.bounds.center.y &&
                bounds.center.z == mesh.bounds.center.z && bounds.extents.x == mesh.bounds.extents.x &&
                bounds.extents.y == mesh.bounds.extents.y && bounds.extents.z == mesh.bounds.extents.z,
                "Geometry bounds differ from finalized positions");
        }

        template<class T>
        void GeometryArray(WireWriter& wire, const std::vector<T>& values)
        {
            Require(values.size() <= kModelSubAssetMaxBytes / sizeof(T), "Geometry array exceeds byte budget");
            wire.Integer(static_cast<std::uint32_t>(values.size()));
            for (const auto value : values)
            {
                wire.Integer(value);
            }
        }

        template<class T>
        void GeometryArray(WireReader& wire, std::vector<T>& values)
        {
            const auto count = wire.Count(kModelSubAssetMaxBytes / sizeof(T), sizeof(T));
            values.reserve(count);
            for (std::uint32_t index = 0; index < count; ++index)
            {
                values.push_back(wire.Integer<T>());
            }
        }

        void GeometryMeshlets(WireWriter& wire, const MeshletPayload& value)
        {
            const auto& settings = value.settings;
            wire.Integer(settings.profileVersion); wire.Integer(settings.builderVersion);
            wire.Integer(settings.meshoptimizerVersion); wire.Integer(settings.maxVertices);
            wire.Integer(settings.maxTriangles); wire.Float(settings.coneWeight);
            wire.Bytes(value.geometryDigest);
            wire.Integer(value.lod0.firstMeshlet); wire.Integer(value.lod0.meshletCount);
            Require(value.descriptors.size() <= kModelSubAssetMaxBytes / 80u, "Too many meshlet descriptors");
            wire.Integer(static_cast<std::uint32_t>(value.descriptors.size()));
            for (const auto& descriptor : value.descriptors)
            {
                wire.Integer(descriptor.vertexOffset); wire.Integer(descriptor.triangleOffset);
                wire.Integer(descriptor.primitiveOffset); wire.Integer(descriptor.vertexCount);
                wire.Integer(descriptor.triangleCount); wire.Integer(descriptor.flags);
                wire.Integer(descriptor.reserved[0]); wire.Integer(descriptor.reserved[1]);
                for (const auto scalar : descriptor.sphereCenter)
                {
                    wire.Float(scalar);
                }
                wire.Float(descriptor.sphereRadius);
                for (const auto scalar : descriptor.coneApex)
                {
                    wire.Float(scalar);
                }
                wire.Integer(descriptor.reservedBounds);
                for (const auto scalar : descriptor.coneAxis)
                {
                    wire.Float(scalar);
                }
                wire.Float(descriptor.coneCutoff);
            }
            GeometryArray(wire, value.vertexRemap);
            GeometryArray(wire, value.triangleIndices);
            GeometryArray(wire, value.primitiveRemap);
        }

        void GeometryMeshlets(WireReader& wire, MeshletPayload& value)
        {
            auto& settings = value.settings;
            settings.profileVersion = wire.Integer<std::uint32_t>();
            settings.builderVersion = wire.Integer<std::uint32_t>();
            settings.meshoptimizerVersion = wire.Integer<std::uint32_t>();
            settings.maxVertices = wire.Integer<std::uint32_t>();
            settings.maxTriangles = wire.Integer<std::uint32_t>();
            settings.coneWeight = wire.Float();
            wire.Bytes(value.geometryDigest);
            value.lod0.firstMeshlet = wire.Integer<std::uint32_t>();
            value.lod0.meshletCount = wire.Integer<std::uint32_t>();
            const auto count = wire.Count(kModelSubAssetMaxBytes / 80u, 80u);
            value.descriptors.reserve(count);
            for (std::uint32_t index = 0; index < count; ++index)
            {
                MeshletDescriptor descriptor;
                descriptor.vertexOffset = wire.Integer<std::uint32_t>();
                descriptor.triangleOffset = wire.Integer<std::uint32_t>();
                descriptor.primitiveOffset = wire.Integer<std::uint32_t>();
                descriptor.vertexCount = wire.Integer<std::uint32_t>();
                descriptor.triangleCount = wire.Integer<std::uint32_t>();
                descriptor.flags = wire.Integer<std::uint32_t>();
                descriptor.reserved[0] = wire.Integer<std::uint32_t>();
                descriptor.reserved[1] = wire.Integer<std::uint32_t>();
                for (auto& scalar : descriptor.sphereCenter)
                {
                    scalar = wire.Float();
                }
                descriptor.sphereRadius = wire.Float();
                for (auto& scalar : descriptor.coneApex)
                {
                    scalar = wire.Float();
                }
                descriptor.reservedBounds = wire.Integer<std::uint32_t>();
                for (auto& scalar : descriptor.coneAxis)
                {
                    scalar = wire.Float();
                }
                descriptor.coneCutoff = wire.Float();
                value.descriptors.push_back(descriptor);
            }
            GeometryArray(wire, value.vertexRemap);
            GeometryArray(wire, value.triangleIndices);
            GeometryArray(wire, value.primitiveRemap);
        }

        void GeometryLods(WireWriter& wire, const MeshLodChain& value)
        {
            const auto& settings = value.settings;
            wire.Integer(settings.builderVersion); wire.Integer(settings.meshoptimizerVersion);
            wire.Integer(settings.levelCount); wire.Float(settings.reductionRatio);
            wire.Float(settings.targetRelativeError); wire.Integer(settings.flags);
            wire.Bytes(value.geometryDigest);
            Require(value.levels.size() <= kMeshLodMaxLevels, "Too many geometry LOD levels");
            wire.Integer(static_cast<std::uint32_t>(value.levels.size()));
            for (const auto& level : value.levels)
            {
                wire.Float(level.geometricError);
                GeometryArray(wire, level.indices);
                GeometryMeshlets(wire, level.meshlets);
            }
        }

        void GeometryLods(WireReader& wire, MeshLodChain& value)
        {
            auto& settings = value.settings;
            settings.builderVersion = wire.Integer<std::uint32_t>();
            settings.meshoptimizerVersion = wire.Integer<std::uint32_t>();
            settings.levelCount = wire.Integer<std::uint32_t>();
            settings.reductionRatio = wire.Float();
            settings.targetRelativeError = wire.Float();
            settings.flags = wire.Integer<std::uint32_t>();
            wire.Bytes(value.geometryDigest);
            const auto count = wire.Count(kMeshLodMaxLevels, 8u);
            value.levels.reserve(count);
            for (std::uint32_t index = 0; index < count; ++index)
            {
                MeshLodLevel level;
                level.geometricError = wire.Float();
                GeometryArray(wire, level.indices);
                GeometryMeshlets(wire, level.meshlets);
                value.levels.push_back(std::move(level));
            }
        }

        void GeometrySection(WireWriter& wire, const WireWriter& section)
        {
            wire.Integer(static_cast<std::uint32_t>(section.bytes.size()));
            wire.Bytes(section.bytes);
        }

        std::span<const std::byte> GeometrySection(WireReader& wire)
        {
            const auto count = wire.Count(kModelSubAssetMaxBytes, 1u);
            const auto result = wire.bytes.subspan(wire.offset, count);
            wire.offset += count;
            return result;
        }

        void GeometryVertices(WireWriter& wire, const VertexBuffer& vertices)
        {
            // native 구조체를 복사하지 않아 패딩·호스트 byte order가 wire ABI로 새지 않는다.
            // 속성표 순서의 LE scalar로 기존 packed GPU 배치를 그대로 보존한다.
            for (std::size_t index = 0; index < vertices.size(); ++index)
            {
                for (const auto& attribute : kVertexAttributeTable)
                {
                    if (!Has(vertices.AttributeMask(), attribute.attribute))
                    {
                        continue;
                    }
                    const auto offset = index * vertices.Stride() + OffsetOf(vertices.AttributeMask(), attribute.attribute);
                    const auto source = vertices.Bytes().subspan(offset, SizeOf(attribute.format));
                    if (attribute.format == VertexFormat::RGBA8Uint)
                    {
                        wire.Bytes(source);
                    }
                    else
                    {
                        for (std::size_t scalar = 0; scalar < source.size(); scalar += sizeof(float))
                        {
                            float value{};
                            std::memcpy(&value, source.data() + scalar, sizeof(value));
                            wire.Float(value);
                        }
                    }
                }
            }
        }

        void GeometryVertices(WireReader& wire, VertexAttributeMask attributes, std::uint32_t count,
            VertexBuffer& vertices)
        {
            const auto stride = StrideOf(attributes);
            Require(count <= (wire.bytes.size() - wire.offset) / stride, "Truncated packed geometry vertices");
            std::vector<std::byte> packed(static_cast<std::size_t>(count) * stride);
            for (std::size_t index = 0; index < count; ++index)
            {
                for (const auto& attribute : kVertexAttributeTable)
                {
                    if (!Has(attributes, attribute.attribute))
                    {
                        continue;
                    }
                    const auto offset = index * stride + OffsetOf(attributes, attribute.attribute);
                    const auto size = SizeOf(attribute.format);
                    if (attribute.format == VertexFormat::RGBA8Uint)
                    {
                        for (std::size_t byte = 0; byte < size; ++byte)
                        {
                            packed[offset + byte] = static_cast<std::byte>(wire.Integer<std::uint8_t>());
                        }
                    }
                    else
                    {
                        for (std::size_t scalar = 0; scalar < size; scalar += sizeof(float))
                        {
                            const auto value = wire.Float();
                            std::memcpy(packed.data() + offset + scalar, &value, sizeof(value));
                        }
                    }
                }
            }
            Require(vertices.AssignPacked(attributes, count, packed), "Cannot assign packed geometry vertices");
        }

        void Descriptor(const ModelDescriptorArtifact& value)
        {
            Require(V8(value.modelAssetId) && (!value.skeletonAssetId.IsValid() || V8(value.skeletonAssetId)) &&
                value.modelAssetId != value.skeletonAssetId, "Invalid model descriptor identities");
            Require(value.clips.size() <= kModelSubAssetMaxClips &&
                (value.clips.empty() || value.skeletonAssetId.IsValid()), "Model clips require skeleton");
            Require(value.meshes.size() <= kMaxDescriptorEntries && value.nodes.size() <= kMaxDescriptorEntries &&
                value.materials.size() <= kMaxDescriptorEntries, "Model descriptor exceeds metadata budget");
            std::set<AssetId> ids{ value.modelAssetId };
            if (value.skeletonAssetId.IsValid())
            {
                ids.insert(value.skeletonAssetId);
            }
            for (const auto& clip : value.clips)
            {
                Require(V8(clip.clipAssetId) && ids.insert(clip.clipAssetId).second,
                    "Invalid or duplicate descriptor clip identity");
                Timing(clip.durationTicks, clip.ticksPerSecond);
            }
            std::set<AssetId> materials;
            for (const auto& material : value.materials)
            {
                Require(V8(material.materialAssetId) && ids.insert(material.materialAssetId).second,
                    "Invalid or duplicate descriptor material identity");
                Require(material.blendMode == MaterialBlendMode::Opaque ||
                    material.blendMode == MaterialBlendMode::Transparent || material.blendMode == MaterialBlendMode::Masked,
                    "Invalid descriptor material blend mode");
                materials.insert(material.materialAssetId);
            }
            std::set<AssetId> meshes;
            for (const auto& mesh : value.meshes)
            {
                Require(V8(mesh.meshAssetId) && ids.insert(mesh.meshAssetId).second,
                    "Invalid or duplicate descriptor mesh identity");
                Require(!mesh.materialAssetId.IsValid() || materials.contains(mesh.materialAssetId),
                    "Descriptor mesh references an undeclared material");
                GeometryBounds(mesh.bounds);
                GeometryHeader(mesh.attributes, mesh.stride, mesh.vertexCount, mesh.indexCount);
                Require(mesh.skinned == Has(mesh.attributes, VertexAttribute::BoneIndices) &&
                    (!mesh.skinned || value.skeletonAssetId.IsValid()), "Descriptor mesh skin layout/binding mismatch");
                meshes.insert(mesh.meshAssetId);
            }
            Require(value.meshes.empty() || !value.nodes.empty(), "Descriptor meshes require a node hierarchy");
            std::size_t references{};
            for (std::size_t index = 0; index < value.nodes.size(); ++index)
            {
                const auto& node = value.nodes[index];
                Require(node.parent.IsValid() ? node.parent.Value() < index : index == 0u,
                    "Descriptor hierarchy requires one root and parent-before-child ordering");
                Require(node.meshAssetIds.size() <= kMaxDescriptorEntries &&
                    references + node.meshAssetIds.size() <= kMaxDescriptorEntries,
                    "Descriptor mesh reference budget exceeded");
                references += node.meshAssetIds.size();
                std::set<AssetId> local;
                for (const auto id : node.meshAssetIds)
                {
                    Require(meshes.contains(id) && local.insert(id).second,
                        "Descriptor node contains an unknown or duplicate mesh reference");
                }
                for (const auto& row : node.localTransform.m)
                {
                    for (const auto scalar : row)
                    {
                        Require(std::isfinite(scalar), "Nonfinite descriptor node transform");
                    }
                }
            }
        }

    }

    bool ComputeBoneLayoutDigest(const Skeleton& skeleton, Sha256Digest& out, std::string& failure)
    {
        return Checked([&]
        {
            WireWriter wire;
            Layout(skeleton, wire);
            Sha256Digest digest{};
            Require(ComputeSha256(wire.bytes, digest, failure), "Cannot hash skeleton layout");
            out = digest;
        }, failure);
    }

    bool ValidateAnimationClipBinding(const AnimationClipArtifact& clip,
        const SkeletonArtifact& skeleton, std::string& failure)
    {
        return Checked([&]
        {
            ClipHeader(clip);
            Sha256Digest actual{};
            Require(ComputeBoneLayoutDigest(skeleton.skeleton, actual, failure), "Invalid skeleton bone layout");
            Require(actual == skeleton.boneLayoutSha256 && actual == clip.requiredBoneLayoutSha256 &&
                clip.requiredBoneCount == skeleton.skeleton.bones.size(), "Animation/skeleton ordered layout mismatch");
        }, failure);
    }

    bool ComputeSkinBindingDigest(const Skeleton& skeleton, Sha256Digest& out, std::string& failure)
    {
        return Checked([&]
        {
            WireWriter wire;
            wire.Text("ce.full-skin-binding.v1");
            Layout(skeleton, wire);
            wire.Matrix(skeleton.rootTransform);
            wire.Matrix(skeleton.globalInverseTransform);
            for (const auto& bone : skeleton.bones)
            {
                wire.Matrix(bone.inverseBindMatrix);
            }
            Sha256Digest digest{};
            Require(ComputeSha256(wire.bytes, digest, failure), "Cannot hash full skeleton binding");
            out = digest;
        }, failure);
    }

    bool ValidateModelGeometryBinding(const ModelGeometryArtifact& geometry,
        const SkeletonArtifact& skeleton, std::string& failure)
    {
        return Checked([&]
        {
            GeometryBase(geometry);
            Require(geometry.requiredBoneCount != 0u &&
                geometry.requiredBoneCount == skeleton.skeleton.bones.size(), "Geometry/skeleton bone count mismatch");
            Sha256Digest layout{};
            Sha256Digest binding{};
            Require(ComputeBoneLayoutDigest(skeleton.skeleton, layout, failure) && layout == skeleton.boneLayoutSha256 &&
                ComputeSkinBindingDigest(skeleton.skeleton, binding, failure) &&
                binding == geometry.requiredSkinBindingSha256, "Geometry/skeleton full skin binding mismatch");
        }, failure);
    }

    bool ValidateModelMeshSummary(const ModelMeshSummary& summary,
        const ModelGeometryArtifact& geometry, std::string& failure)
    {
        return Checked([&]
        {
            const auto& mesh = geometry.mesh;
            Require(summary.attributes == mesh.vertices.AttributeMask() && summary.stride == mesh.vertices.Stride() &&
                summary.vertexCount == mesh.vertices.size() && summary.indexCount == mesh.indices.size() &&
                summary.skinned == (geometry.requiredBoneCount != 0u) &&
                summary.bounds.center.x == mesh.bounds.center.x && summary.bounds.center.y == mesh.bounds.center.y &&
                summary.bounds.center.z == mesh.bounds.center.z && summary.bounds.extents.x == mesh.bounds.extents.x &&
                summary.bounds.extents.y == mesh.bounds.extents.y && summary.bounds.extents.z == mesh.bounds.extents.z,
                "Model mesh summary differs from selected geometry artifact");
        }, failure);
    }

    bool ValidateModelDescriptorDependencies(const ModelDescriptorArtifact& descriptor,
        std::span<const AssetDependency> dependencies, std::string& failure)
    {
        return Checked([&]
        {
            Descriptor(descriptor);
            std::set<TypedAssetReference> expected;
            if (descriptor.skeletonAssetId.IsValid())
            {
                expected.insert({ { descriptor.skeletonAssetId, {} }, CookedAssetKind::Skeleton });
            }
            for (const auto& clip : descriptor.clips)
            {
                expected.insert({ { clip.clipAssetId, {} }, CookedAssetKind::AnimationClip });
            }
            for (const auto& mesh : descriptor.meshes)
            {
                expected.insert({ { mesh.meshAssetId, {} }, CookedAssetKind::Mesh });
            }
            for (const auto& material : descriptor.materials)
            {
                expected.insert({ { material.materialAssetId, {} }, CookedAssetKind::Material });
            }
            Require(dependencies.size() == expected.size(), "Model descriptor loadable edge count mismatch");
            for (const auto& edge : dependencies)
            {
                Require(edge.kind == AssetDependencyKind::Loadable &&
                    (edge.scope == AssetDependencyScope::Internal || edge.scope == AssetDependencyScope::External) &&
                    expected.erase(edge.target) == 1u, "Model descriptor has an undeclared, duplicate or non-loadable edge");
            }
            Require(expected.empty(), "Model descriptor is missing declared loadable edges");
        }, failure);
    }

    bool WriteModelGeometryArtifact(const ModelGeometryArtifact& value,
        std::vector<std::byte>& out, std::string& failure)
    {
        return Checked([&]
        {
            GeometryBase(value);
            std::string diagnostic;
            Require(importer::ValidateMeshlets(value.mesh, value.mesh.meshlets, diagnostic),
                "Invalid geometry meshlet payload");
            Require(importer::ValidateMeshLods(value.mesh, value.mesh.coarseLods, diagnostic),
                "Invalid geometry LOD payload");
            WireWriter wire;
            wire.Header(kGeometryMagic, kModelGeometryArtifactVersion);
            wire.Integer(kVertexLayoutTableHash);
            wire.Integer(value.mesh.vertices.AttributeMask());
            wire.Integer(value.mesh.vertices.Stride());
            wire.Integer(VertexLayoutHash(value.mesh.vertices.AttributeMask()));
            wire.Integer(static_cast<std::uint32_t>(value.mesh.vertices.size()));
            wire.Integer(static_cast<std::uint32_t>(value.mesh.indices.size()));
            GeometryBounds(wire, value.mesh.bounds);
            wire.Integer(value.requiredBoneCount);
            wire.Bytes(value.requiredSkinBindingSha256);
            GeometryVertices(wire, value.mesh.vertices);
            for (const auto index : value.mesh.indices)
            {
                wire.Integer(index);
            }
            WireWriter meshlets;
            GeometryMeshlets(meshlets, value.mesh.meshlets);
            GeometrySection(wire, meshlets);
            WireWriter lods;
            GeometryLods(lods, value.mesh.coarseLods);
            GeometrySection(wire, lods);
            out = std::move(wire.bytes);
        }, failure);
    }

    bool ReadModelGeometryArtifact(std::span<const std::byte> bytes,
        ModelGeometryArtifact& out, std::string& failure, std::vector<std::string>* warnings)
    {
        return Checked([&]
        {
            WireReader wire(bytes);
            wire.Header(kGeometryMagic, kModelGeometryArtifactVersion);
            Require(wire.Integer<std::uint64_t>() == kVertexLayoutTableHash, "Unsupported geometry vertex ABI");
            const auto attributes = wire.Integer<std::uint32_t>();
            const auto stride = wire.Integer<std::uint32_t>();
            Require(wire.Integer<std::uint64_t>() == VertexLayoutHash(attributes), "Geometry layout hash mismatch");
            const auto vertexCount = wire.Integer<std::uint32_t>();
            const auto indexCount = wire.Integer<std::uint32_t>();
            GeometryHeader(attributes, stride, vertexCount, indexCount);
            ModelGeometryArtifact value;
            value.mesh.bounds = GeometryBounds(wire);
            value.requiredBoneCount = wire.Integer<std::uint32_t>();
            wire.Bytes(value.requiredSkinBindingSha256);
            GeometryVertices(wire, attributes, vertexCount, value.mesh.vertices);
            Require(indexCount <= (wire.bytes.size() - wire.offset) / sizeof(std::uint32_t), "Truncated geometry indices");
            value.mesh.indices.reserve(indexCount);
            for (std::uint32_t index = 0; index < indexCount; ++index)
            {
                value.mesh.indices.push_back(wire.Integer<std::uint32_t>());
            }
            GeometryBase(value);
            const auto meshletBytes = GeometrySection(wire);
            const auto lodBytes = GeometrySection(wire);
            wire.End();
            std::vector<std::string> diagnostics;
            std::string diagnostic;
            const bool meshletsValid = Checked([&]
            {
                WireReader section(meshletBytes);
                GeometryMeshlets(section, value.mesh.meshlets);
                section.End();
                Require(importer::ValidateMeshlets(value.mesh, value.mesh.meshlets, diagnostic),
                    "Malformed or stale optional meshlet payload");
            }, diagnostic);
            if (!meshletsValid)
            {
                value.mesh.meshlets = {};
                diagnostics.push_back(diagnostic + "; retaining indexed geometry");
            }
            const bool lodsValid = Checked([&]
            {
                WireReader section(lodBytes);
                GeometryLods(section, value.mesh.coarseLods);
                section.End();
                Require(importer::ValidateMeshLods(value.mesh, value.mesh.coarseLods, diagnostic),
                    "Malformed or stale optional coarse LOD payload");
            }, diagnostic);
            if (!lodsValid)
            {
                value.mesh.coarseLods = {};
                diagnostics.push_back(diagnostic + "; retaining LOD0 geometry");
            }
            if (warnings)
            {
                *warnings = std::move(diagnostics);
            }
            out = std::move(value);
        }, failure);
    }

    bool WriteSkeletonArtifact(const SkeletonArtifact& value, std::vector<std::byte>& out, std::string& failure)
    {
        return Checked([&]
        {
            Require(value.skeleton.clips.empty(), "Skeleton payload cannot embed clips");
            Sha256Digest digest{};
            Require(ComputeBoneLayoutDigest(value.skeleton, digest, failure) && digest == value.boneLayoutSha256,
                "Skeleton layout digest mismatch");
            WireWriter wire;
            wire.Header(kSkeletonMagic, kSkeletonArtifactVersion);
            wire.Bytes(digest);
            wire.Integer(value.skeleton.rootBone.Value());
            wire.Matrix(value.skeleton.rootTransform);
            wire.Matrix(value.skeleton.globalInverseTransform);
            wire.Integer(static_cast<std::uint32_t>(value.skeleton.bones.size()));
            for (const auto& bone : value.skeleton.bones)
            {
                wire.Text(bone.name);
                wire.Integer(bone.parent.Value());
                wire.Matrix(bone.inverseBindMatrix);
            }
            out = std::move(wire.bytes);
        }, failure);
    }

    bool ReadSkeletonArtifact(std::span<const std::byte> bytes, SkeletonArtifact& out, std::string& failure)
    {
        return Checked([&]
        {
            WireReader wire(bytes);
            wire.Header(kSkeletonMagic, kSkeletonArtifactVersion);
            SkeletonArtifact value;
            wire.Bytes(value.boneLayoutSha256);
            value.skeleton.rootBone = BoneIndex(wire.Integer<std::uint32_t>());
            value.skeleton.rootTransform = wire.Matrix();
            value.skeleton.globalInverseTransform = wire.Matrix();
            const auto count = wire.Count(kModelSubAssetMaxBones, 72u);
            value.skeleton.bones.reserve(count);
            for (std::uint32_t index = 0; index < count; ++index)
            {
                Bone bone;
                bone.name = wire.Text();
                bone.parent = BoneIndex(wire.Integer<std::uint32_t>());
                bone.inverseBindMatrix = wire.Matrix();
                value.skeleton.bones.push_back(std::move(bone));
            }
            wire.End();
            Sha256Digest digest{};
            Require(ComputeBoneLayoutDigest(value.skeleton, digest, failure) && digest == value.boneLayoutSha256,
                "Skeleton layout digest mismatch");
            out = std::move(value);
        }, failure);
    }

    bool WriteAnimationClipArtifact(const AnimationClipArtifact& value, std::vector<std::byte>& out, std::string& failure)
    {
        return Checked([&]
        {
            ClipHeader(value);
            WireWriter wire;
            wire.Header(kClipMagic, kAnimationClipArtifactVersion);
            wire.Bytes(value.skeletonAssetId.value.data);
            wire.Bytes(value.requiredBoneLayoutSha256);
            wire.Integer(value.requiredBoneCount);
            wire.Text(value.clip.name);
            wire.Double(value.clip.durationTicks); wire.Double(value.clip.ticksPerSecond);
            wire.Integer(static_cast<std::uint8_t>(value.clip.looping));
            wire.Integer(static_cast<std::uint32_t>(value.clip.channels.size()));
            std::set<std::uint32_t> channels;
            std::uint64_t total{};
            for (const auto& channel : value.clip.channels)
            {
                Require(IsInRange(channel.bone, value.requiredBoneCount) && channels.insert(channel.bone.Value()).second,
                    "Invalid or duplicate animation channel bone");
                wire.Integer(channel.bone.Value());
                for (const auto mode : { channel.translationInterpolation, channel.rotationInterpolation, channel.scaleInterpolation })
                {
                    Mode(mode);
                    wire.Integer(static_cast<std::uint8_t>(mode));
                }
                Keys(wire, channel.translations, value.clip.durationTicks, total);
                Keys(wire, channel.rotations, value.clip.durationTicks, total);
                Keys(wire, channel.scales, value.clip.durationTicks, total);
            }
            out = std::move(wire.bytes);
        }, failure);
    }

    bool ReadAnimationClipArtifact(std::span<const std::byte> bytes, AnimationClipArtifact& out, std::string& failure)
    {
        return Checked([&]
        {
            WireReader wire(bytes);
            wire.Header(kClipMagic, kAnimationClipArtifactVersion);
            AnimationClipArtifact value;
            wire.Bytes(value.skeletonAssetId.value.data);
            wire.Bytes(value.requiredBoneLayoutSha256);
            value.requiredBoneCount = wire.Integer<std::uint32_t>();
            value.clip.name = wire.Text();
            value.clip.durationTicks = wire.Double(); value.clip.ticksPerSecond = wire.Double();
            const auto looping = wire.Integer<std::uint8_t>();
            Require(looping <= 1u, "Invalid animation loop flag");
            value.clip.looping = looping != 0u;
            ClipHeader(value);
            const auto count = wire.Count(value.requiredBoneCount, 19u);
            value.clip.channels.reserve(count);
            std::set<std::uint32_t> channels;
            std::uint64_t total{};
            for (std::uint32_t index = 0; index < count; ++index)
            {
                AnimationChannel channel;
                channel.bone = BoneIndex(wire.Integer<std::uint32_t>());
                Require(IsInRange(channel.bone, value.requiredBoneCount) && channels.insert(channel.bone.Value()).second,
                    "Invalid or duplicate animation channel bone");
                channel.translationInterpolation = static_cast<InterpolationMode>(wire.Integer<std::uint8_t>());
                channel.rotationInterpolation = static_cast<InterpolationMode>(wire.Integer<std::uint8_t>());
                channel.scaleInterpolation = static_cast<InterpolationMode>(wire.Integer<std::uint8_t>());
                Mode(channel.translationInterpolation); Mode(channel.rotationInterpolation); Mode(channel.scaleInterpolation);
                Keys(wire, channel.translations, value.clip.durationTicks, total);
                Keys(wire, channel.rotations, value.clip.durationTicks, total);
                Keys(wire, channel.scales, value.clip.durationTicks, total);
                value.clip.channels.push_back(std::move(channel));
            }
            wire.End();
            out = std::move(value);
        }, failure);
    }

    bool WriteModelDescriptorArtifact(const ModelDescriptorArtifact& value, std::vector<std::byte>& out, std::string& failure)
    {
        return Checked([&]
        {
            Descriptor(value);
            WireWriter wire;
            wire.Header(kDescriptorMagic, kModelDescriptorVersion);
            wire.Bytes(value.modelAssetId.value.data); wire.Bytes(value.skeletonAssetId.value.data);
            wire.Text(value.name);
            wire.Integer(static_cast<std::uint32_t>(value.clips.size()));
            for (const auto& clip : value.clips)
            {
                wire.Bytes(clip.clipAssetId.value.data);
                wire.Text(clip.name);
                wire.Double(clip.durationTicks); wire.Double(clip.ticksPerSecond);
                wire.Integer(static_cast<std::uint8_t>(clip.looping));
            }
            wire.Integer(kVertexLayoutTableHash);
            wire.Integer(static_cast<std::uint32_t>(value.meshes.size()));
            for (const auto& mesh : value.meshes)
            {
                wire.Bytes(mesh.meshAssetId.value.data); wire.Bytes(mesh.materialAssetId.value.data);
                wire.Text(mesh.name);
                GeometryBounds(wire, mesh.bounds);
                wire.Integer(mesh.attributes); wire.Integer(mesh.stride);
                wire.Integer(mesh.vertexCount); wire.Integer(mesh.indexCount);
                wire.Integer(static_cast<std::uint8_t>(mesh.skinned));
            }
            wire.Integer(static_cast<std::uint32_t>(value.nodes.size()));
            for (const auto& node : value.nodes)
            {
                wire.Text(node.name);
                wire.Integer(node.parent.Value());
                wire.Matrix(node.localTransform);
                wire.Integer(static_cast<std::uint32_t>(node.meshAssetIds.size()));
                for (const auto id : node.meshAssetIds)
                {
                    wire.Bytes(id.value.data);
                }
            }
            wire.Integer(static_cast<std::uint32_t>(value.materials.size()));
            for (const auto& material : value.materials)
            {
                wire.Bytes(material.materialAssetId.value.data);
                wire.Text(material.name);
                wire.Integer(static_cast<std::uint8_t>(material.blendMode));
            }
            wire.Integer(static_cast<std::uint8_t>(value.createMeshCollider));
            out = std::move(wire.bytes);
        }, failure);
    }

    bool ReadModelDescriptorArtifact(std::span<const std::byte> bytes, ModelDescriptorArtifact& out, std::string& failure)
    {
        return Checked([&]
        {
            WireReader wire(bytes);
            // v1을 빈 v2로 받아들이면 node/mesh가 조용히 사라진다. 재cook만 허용한다.
            wire.Header(kDescriptorMagic, kModelDescriptorVersion);
            ModelDescriptorArtifact value;
            wire.Bytes(value.modelAssetId.value.data); wire.Bytes(value.skeletonAssetId.value.data);
            value.name = wire.Text();
            const auto count = wire.Count(kModelSubAssetMaxClips, 37u);
            value.clips.reserve(count);
            for (std::uint32_t index = 0; index < count; ++index)
            {
                ModelClipSummary clip;
                wire.Bytes(clip.clipAssetId.value.data);
                clip.name = wire.Text();
                clip.durationTicks = wire.Double(); clip.ticksPerSecond = wire.Double();
                const auto looping = wire.Integer<std::uint8_t>();
                Require(looping <= 1u, "Invalid clip summary loop flag");
                clip.looping = looping != 0u;
                value.clips.push_back(std::move(clip));
            }
            Require(wire.Integer<std::uint64_t>() == kVertexLayoutTableHash, "Unsupported descriptor vertex ABI");
            const auto meshCount = wire.Count(kMaxDescriptorEntries, 77u);
            value.meshes.reserve(meshCount);
            for (std::uint32_t index = 0; index < meshCount; ++index)
            {
                ModelMeshSummary mesh;
                wire.Bytes(mesh.meshAssetId.value.data); wire.Bytes(mesh.materialAssetId.value.data);
                mesh.name = wire.Text();
                mesh.bounds = GeometryBounds(wire);
                mesh.attributes = wire.Integer<std::uint32_t>(); mesh.stride = wire.Integer<std::uint32_t>();
                mesh.vertexCount = wire.Integer<std::uint32_t>(); mesh.indexCount = wire.Integer<std::uint32_t>();
                const auto skinned = wire.Integer<std::uint8_t>();
                Require(skinned <= 1u, "Invalid mesh summary skin flag");
                mesh.skinned = skinned != 0u;
                value.meshes.push_back(std::move(mesh));
            }
            const auto nodeCount = wire.Count(kMaxDescriptorEntries, 76u);
            value.nodes.reserve(nodeCount);
            std::size_t references{};
            for (std::uint32_t index = 0; index < nodeCount; ++index)
            {
                ModelNodeSummary node;
                node.name = wire.Text();
                node.parent = NodeIndex(wire.Integer<std::uint32_t>());
                node.localTransform = wire.Matrix();
                const auto referenceCount = wire.Count(kMaxDescriptorEntries, 16u);
                Require(references + referenceCount <= kMaxDescriptorEntries, "Descriptor mesh reference budget exceeded");
                references += referenceCount;
                node.meshAssetIds.resize(referenceCount);
                for (auto& id : node.meshAssetIds)
                {
                    wire.Bytes(id.value.data);
                }
                value.nodes.push_back(std::move(node));
            }
            const auto materialCount = wire.Count(kMaxDescriptorEntries, 21u);
            value.materials.reserve(materialCount);
            for (std::uint32_t index = 0; index < materialCount; ++index)
            {
                ModelMaterialSummary material;
                wire.Bytes(material.materialAssetId.value.data);
                material.name = wire.Text();
                material.blendMode = static_cast<MaterialBlendMode>(wire.Integer<std::uint8_t>());
                value.materials.push_back(std::move(material));
            }
            const auto createMeshCollider = wire.Integer<std::uint8_t>();
            Require(createMeshCollider <= 1u, "Invalid model collider policy flag");
            value.createMeshCollider = createMeshCollider != 0u;
            wire.End();
            Descriptor(value);
            out = std::move(value);
        }, failure);
    }
}
