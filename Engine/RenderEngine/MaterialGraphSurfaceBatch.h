#pragma once

#include "LXMaterialPipeline.h"

#include "MaterialGraphIblBake.h"
#include "MaterialGraphRenderBindings.h"
#include "Render/Graph/EnhancedRenderGraph.h"

#include <atomic>

namespace material_graph
{
    class MeshSurfaceBatch;
    // World-space frame after object/skin transforms. UV/lod comes from the host's
    // sampling policy; this API does not invent pixel derivatives or interpolation.
    struct SurfacePoint
    {
        IblVector uvLod{};
        IblVector position{};
        IblVector normal{0, 0, 1, 0};
        IblVector tangent{1, 0, 0, 0};
        IblVector bitangent{0, 1, 0, 0};
    };
    static_assert(sizeof(SurfacePoint) == 80);

    // A missing tangent basis uses the geometric normal. A lone missing axis is
    // still ambiguous (for example, a mirrored seam cancelling its bitangent).
    bool ValidSurfaceTangentPair(const SurfacePoint& point);

    struct SurfaceView
    {
        IblVector eye{0, 0, 1, 0};
        std::uint64_t sceneEpoch{}, viewRevision{}, geometryRevision{};
    };

    // GPU geometry transport shared by mesh samples and visible raster pixels.
    // Validation/coverage is queried on the owning render thread after completion.
    class SurfaceGeometrySource
    {
      public:
        virtual ~SurfaceGeometrySource() = default;
        virtual RHIBufferHandle Buffer() const = 0;
        virtual std::uint32_t Count() const = 0;
        virtual const IRenderDeviceServices* Device() const = 0;
        virtual const SurfaceView& View() const = 0;
        virtual std::uint64_t RecordingId() const = 0;
        virtual bool IsValidated() const = 0;
        virtual bool IsReadyForEvaluation() const { return true; }
        virtual bool IsPreparedForGraph() const { return false; }
        virtual RGHandle GraphOutput(const EnhancedRenderGraph&) const { return {}; }
        virtual bool IsCovered(std::uint32_t index) const { return index < Count(); }
        virtual bool ValidateReadback(std::span<const SurfacePoint> points, std::string& error) const = 0;
    };

    // Authoring/cooker host: exact specialized CS source and reflected material
    // binding layout. Runtime consumes the verified/cooked artifact, not Slang.
    std::string BuildSurfaceSource(const LX::LXMaterialProgram& program);
    bool VerifySurfaceProduct(const LX::LXMaterialProgram& program, const std::filesystem::path& file,
                              std::span<const CompileTarget> targets, RHIShaderCompileOptions options, const Budget& budget,
                              VerifiedProduct& result, std::vector<LX::LXMaterialDiagnostic>& diagnostics);

    class SurfaceBatch
    {
      public:
        ~SurfaceBatch();
        SurfaceBatch(const SurfaceBatch&) = delete;
        SurfaceBatch& operator=(const SurfaceBatch&) = delete;

        RHIBufferHandle Buffer() const { return buffer_; }
        std::uint32_t Count() const { return count_; }
        const IRenderDeviceServices* Device() const { return device_; }
        const SurfaceView& View() const { return view_; }
        own::shared_owner<const Instance> Material() const
        {
            return bindings_->instancePins->Owner(bindings_->instancePinIndex);
        }
        const own::shared_owner<InstanceFramePins>& MaterialPins() const
        {
            return bindings_->instancePins;
        }
        std::uint64_t RecordingId() const { return recordingId_; }
        bool IsValidated() const { return validated_; }
        bool IsReadyForBake() const { return recordedStages_.load() == 3 && (!mesh_ || mesh_->IsReadyForEvaluation()); }
        bool IsPreparedForGraph() const;
        RGHandle GraphOutput(const EnhancedRenderGraph& graph) const;
        // Declare after the source producer in the same graph. The graph owns all
        // transitions; callbacks only bind the immutable packet and dispatch.
        bool Declare(EnhancedRenderGraph& graph, std::string& error) const;

        // No collision-prone key: immutable instance identity, view/revisions and
        // exact sample bytes. Reuse still requires the host to touch texture/cache
        // residency and retain all GPU owners until the last submission completes.
        bool Matches(const Instance& instance, const SurfaceView& view, std::span<const SurfacePoint> points) const;
        bool Matches(const Instance& instance, const MeshSurfaceBatch& mesh) const;
        bool Matches(const Instance& instance, const SurfaceGeometrySource& source) const;

        // Inspect a completed GPU readback before accepting a batch for Scene
        // publication. An invalid point is explicit (viewTier.w = -1), never a
        // silently clamped valid material. Record/RecordGpu alone is not acceptance.
        bool ValidateReadback(std::span<const IblBakePoint> points, std::string& error) const;

      private:
        friend class SurfaceEvaluator;
        SurfaceBatch() = default;
        bool RecordCommands(RHIEncoder& encoder, std::string& error) const;
        IRenderDeviceServices* device_{};
        RHIBufferHandle buffer_;
        SurfaceView view_;
        std::vector<SurfacePoint> points_;
        std::shared_ptr<const SurfaceGeometrySource> mesh_;
        std::uint32_t count_{};
        own::shared_owner<const RenderBindings> bindings_;
        std::uint64_t recordingId_{};
        std::uint64_t descriptorVersion_{};
        std::shared_ptr<const LX::Runtime::ComputeGeneration> pipeline_;
        RHIBufferSlice inputs_, uniform_;
        RHIBindingTable output_;
        std::weak_ptr<const SurfaceBatch> self_;
        mutable std::atomic<unsigned> recordedStages_{};
        mutable std::atomic<bool> declared_{};
        mutable const EnhancedRenderGraph* graph_{};
        mutable std::uint64_t graphEpoch_{};
        mutable RGHandle graphOutput_;
        mutable bool validated_{};
    };

    class SurfaceEvaluator
    {
      public:
        // Candidate-first PSO/layout initialization; failure preserves the accepted
        // evaluator. A new graph generation requires its own matching evaluator.
        bool Initialize(IRenderDeviceServices& device, IRenderRootSignatureCache& roots, IRenderPipelineCache& pipelines,
                        const VerifiedProduct& product, RHIShaderBinary backend, const Budget& budget, std::string& error);
        const PassLayout& Layout() const { return layout_; }

        // Immediate recording only, after material textures are ShaderResource.
        // All allocations/bind validation precede dispatch. Keep returned owner
        // through submission completion/abort and release it before device shutdown.
        bool Record(IRenderDeviceServices& device, own::shared_owner<const RenderBindings> bindings, const SurfaceView& view,
                    std::span<const SurfacePoint> points, std::shared_ptr<const SurfaceBatch>& result, std::string& error);
        // GPU skin/world frame -> graph -> IBL, without a CPU readback between stages.
        // Across recordings the source must have passed completed readback validation.
        bool RecordGpu(IRenderDeviceServices& device, own::shared_owner<const RenderBindings> bindings,
                       std::shared_ptr<const SurfaceGeometrySource> mesh, std::shared_ptr<const SurfaceBatch>& result,
                       std::string& error);
        // Allocate without recording, including an unrecorded prepared raster
        // source. PrepareParallel must precede this when using worker recording.
        bool PrepareGpu(IRenderDeviceServices& device, own::shared_owner<const RenderBindings> bindings,
                        std::shared_ptr<const SurfaceGeometrySource> source, std::shared_ptr<const SurfaceBatch>& result,
                        std::string& error);

      private:
        bool RecordInputs(IRenderDeviceServices& device, own::shared_owner<const RenderBindings> bindings,
                          const SurfaceView& view, std::span<const SurfacePoint> points,
                          std::shared_ptr<const SurfaceGeometrySource> mesh, std::shared_ptr<const SurfaceBatch>& result,
                          std::string& error);
        bool PrepareInputs(IRenderDeviceServices& device, own::shared_owner<const RenderBindings> bindings,
                           const SurfaceView& view, std::span<const SurfacePoint> points,
                           std::shared_ptr<const SurfaceGeometrySource> source, std::shared_ptr<const SurfaceBatch>& result,
                           std::string& error);
        IRenderDeviceServices* device_{};
        std::shared_ptr<const LX::Runtime::ComputeGeneration> pipeline_;
        PassLayout layout_;
        std::string semanticKey_;
        bool principledGgx_{};
    };
} // namespace material_graph
