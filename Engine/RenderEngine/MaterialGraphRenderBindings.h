#pragma once
#include "../Utility_Framework/Ownership.h"

#include "MaterialGraphRuntime.h"
#include "MaterialGraphInstancePins.h"
#include "TextureFramePins.h"
#include "RHI/IRenderDeviceServices.h"
#include "RHI/IRenderTextureCache.h"
#include "RHI/RHIEncoder.h"

#include "../Utility_Framework/StandardLibrary.h"

namespace material_graph
{
    struct PassLayout
    {
        RHIPipelineLayoutHandle handle;
        BindingLayout material;
        std::uint32_t hostParameterCount{};
        std::optional<std::uint32_t> uniformSlot;
        std::optional<std::uint32_t> textureSlot;
        std::optional<std::uint32_t> samplerSlot;
        std::optional<std::uint32_t> descriptorIndexSlot;
        bool operator==(const PassLayout&) const = default;
    };

    // Host ranges are kept intact. Material ranges are appended only when used;
    // conflicting host constants, SRVs or static/dynamic samplers are rejected.
    bool CreatePassLayout(IRenderRootSignatureCache& cache, const BindingLayout& material,
                          std::span<const RHIPipelineLayoutParam> hostParameters,
                          std::span<const RHIStaticSamplerDesc> hostSamplers, bool inputAssembler, PassLayout& result,
                          std::string& error);

    struct BindlessMaterialFrame
    {
        std::mutex mutex;
        std::vector<RHIBindingDesc> descriptions;
        using TextureKey = std::tuple<std::uint32_t, RHIFormat, std::uint32_t, std::uint32_t>;
        // Index + 1; zero is empty. Start with 128 slots and grow at half load.
        // Registration is serialized by mutex; submitted frames retain this table.
        // Small frames never allocate/zero the maximum 32 KiB index.
        std::vector<std::uint32_t> textureSlots;
        std::uint32_t RegisterTexture(const RHIBindingDesc& description);
        RHIBindingTable textures;
        std::size_t publishedSize{};
        // Frame ownership pins complete immutable table values, including backend
        // version. Readers never touch the mutable descriptions or owner vector.
        std::vector<own::unique_owner<const RHIBindingTable>> publishedTables;
        std::atomic<const RHIBindingTable*> publishedTable{};
    };

    struct RenderResourceView
    {
        std::span<const std::uint8_t> uniforms;
        std::span<const RHIBindingDesc> textures;
        std::span<const RHISamplerDesc> samplers;
        std::span<const own::shared_owner<const Texture>> owners;
    };

    struct RenderBindings
    {
        own::shared_owner<InstanceFramePins> instancePins;
        std::size_t instancePinIndex{InstanceFramePins::InvalidIndex};
        own::local_view<const Instance> instance;
        // The immutable program owns the reflection snapshot; packets retain it
        // independently through frame retirement, without copying its vectors.
        own::shared_owner<const PassLayout> layout;
        // CPU bytes and sampler descriptions are immutable across recordings.
        // Only the texture registration handles belong to this recording.
        own::shared_owner<const ResourcePacket> resourceOwner;
        std::array<RHIBindingDesc, 64> textureValues{};
        RenderResourceView resources;
        RHIBufferSlice uniforms;
        RHIBindingTable textures;
        RHISamplerTable samplers;
        own::shared_owner<BindlessMaterialFrame> bindless;
        RHIBufferSlice descriptorIndices;
        // CPU values for the recording's shared packet material table. GPU upload
        // addresses are deliberately not persisted as material identity.
        std::array<std::uint32_t, 128> descriptorIndexValues{};
        const IRenderDeviceServices* device{};
        std::uint64_t recordingId{};
        std::uint64_t descriptorVersion{};
    };

    // One render owner per device. Texture cache owns GPU registrations and their
    // fence retirement; the packet retains CPU/program owners through submission.
    // Rebuild packets every recording so texture-cache use tracking stays current.
    // Immutable CPU descriptions survive recordings, with exact weak instance
    // identity and texture-shape checks. They contain no frame/native handles.
    // RenderGraph declares shader reads/transitions; Bind does not change states.
    class RenderBindingCache
    {
      public:
        struct PreparedPass
        {
          private:
            friend class RenderBindingCache;
            const RenderBindings* source_{};
            RHIPipelineLayoutHandle layout_;
            RHIBindingTable textures_;
            std::uint64_t recordingId_{}, descriptorVersion_{};
        };

        bool Prepare(IRenderDeviceServices& device, IRenderTextureCache& textures,
                     const own::shared_owner<const Instance>& instance, const PassLayout& layout,
                     own::shared_owner<const RenderBindings>& result, std::string& error,
                     const own::shared_owner<InstanceFramePins>& instancePins = {},
                     const TextureFramePins* imagePins = nullptr,
                     const own::shared_owner<const PassLayout>& layoutSnapshot = {});

        // Synchronous preparation borrows the instance. On a cache miss the packet
        // explicitly retains its pin table and layout through RHI/GPU retirement.
        bool PrepareBorrowed(IRenderDeviceServices& device, IRenderTextureCache& textures,
                             own::local_view<const Instance> instance, const PassLayout& layout,
                             own::shared_owner<const RenderBindings>& result, std::string& error,
                             const own::shared_owner<InstanceFramePins>& instancePins,
                             const TextureFramePins* imagePins = nullptr,
                             const own::shared_owner<const PassLayout>& layoutSnapshot = {});

        // Call after SetPipeline with this packet's layout, before a draw/dispatch.
        // Frame upload/descriptor handles cannot be reused in another recording.
        static bool Bind(IRenderDeviceServices& device, RHIEncoder& encoder, RHIBindPoint point,
                         const RenderBindings& bindings, std::string& error);
        static bool Validate(const IRenderDeviceServices& device, const RenderBindings& bindings, std::string& error);
        // A second pass in the same recording can consume the same material uploads
        // with another host root layout. No cross-frame descriptor reuse is allowed.
        static bool ValidatePass(const IRenderDeviceServices& device, const RenderBindings& source,
                                 const PassLayout& layout, std::string& error);
        static bool BindPass(IRenderDeviceServices& device, RHIEncoder& encoder, RHIBindPoint point,
                             const RenderBindings& source, const PassLayout& layout, std::string& error);

        // immediate 경로는 인코더 획득/자원 참조 기록 전에 준비한다. PreparedPass는
        // source를 빌리므로 호출자가 BindPreparedPass가 끝날 때까지 원본 packet을
        // 소유해야 한다. 바인딩은 이 테이블 스냅샷만 쓰며 CreateBindings/재게시를 하지 않는다.
        static bool PreparePass(IRenderDeviceServices& device, const RenderBindings& source, const PassLayout& layout,
                                PreparedPass& result, std::string& error);
        static bool BindPreparedPass(IRenderDeviceServices& device, RHIEncoder& encoder, RHIBindPoint point,
                                     const RenderBindings& source, const PassLayout& layout,
                                     const PreparedPass& prepared, std::string& error);

        // Clear after device idle, before device recreation. Sampler registrations
        // are device-owned; the RHI has no per-table release operation.
        void Clear();
        std::size_t SamplerTableCount() const { return bindlessSamplers_.IsValid() ? 1 : 0; }

      private:
        static void BindPassCommands(RHIEncoder& encoder, RHIBindPoint point, const RenderBindings& source,
                                     const PassLayout& layout, const PreparedPass& prepared);
        struct CpuDescription
        {
            own::weak_owner<const Instance> instance;
            own::shared_owner<const ResourcePacket> resources;
            std::vector<RHITextureEntry> textureShapes;
            std::array<std::uint32_t, 128> descriptorIndices{};
            std::uint64_t lastUse{};
            std::size_t bytes{};
        };
        IRenderDeviceServices* device_{};
        std::map<InstanceFramePins::Key, std::vector<own::weak_owner<const RenderBindings>>> prepared_;
        std::uint64_t preparedRecording_{}, preparedDescriptorVersion_{};
        own::shared_owner<BindlessMaterialFrame> bindless_;
        RHISamplerTable bindlessSamplers_;
        std::map<InstanceFramePins::Key, CpuDescription> descriptions_;
        std::uint64_t descriptionClock_{};
        std::size_t descriptionBytes_{};
    };
} // namespace material_graph
