#include "CookedModelSubAssetCodec.h"
#include "../../Assets/AssetIdentityProfile.h"

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

        void Descriptor(const ModelDescriptorArtifact& value)
        {
            Require(V8(value.modelAssetId) && (!value.skeletonAssetId.IsValid() || V8(value.skeletonAssetId)),
                "Invalid model descriptor identities");
            Require(value.clips.size() <= kModelSubAssetMaxClips &&
                (value.clips.empty() || value.skeletonAssetId.IsValid()), "Model clips require skeleton");
            std::set<AssetId> ids;
            for (const auto& clip : value.clips)
            {
                Require(V8(clip.clipAssetId) && clip.clipAssetId != value.modelAssetId &&
                    clip.clipAssetId != value.skeletonAssetId && ids.insert(clip.clipAssetId).second,
                    "Invalid or duplicate descriptor clip identity");
                Timing(clip.durationTicks, clip.ticksPerSecond);
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
            out = std::move(wire.bytes);
        }, failure);
    }

    bool ReadModelDescriptorArtifact(std::span<const std::byte> bytes, ModelDescriptorArtifact& out, std::string& failure)
    {
        return Checked([&]
        {
            WireReader wire(bytes);
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
            wire.End();
            Descriptor(value);
            out = std::move(value);
        }, failure);
    }
}
