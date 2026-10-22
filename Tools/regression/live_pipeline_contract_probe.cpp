// Runs production LivePipelineDesc against the production graph. Only native
// services and the platform-heavy frame-context payload are fixture boundaries.
#include "Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.h"
#include "Engine/RenderEngine/Render/Core/EnhancedLightPacking.h"
#include "Engine/RenderEngine/Render/Core/RenderFeatureContributor.h"
#include "fixtures/render-graph-allocation-counters.h"
#include "fixtures/render-graph-portable-services.h"
#include <mathematics/transform.hpp>
#include <iostream>

namespace
{
    size_t checks{};
    constexpr LiveResourceSlot<LiveResourceSemantic::LinearColor> kInput{"Test.Input", RHIFormat::RGBA16Float,
                                                                         LiveResourceExtent::Render};
    constexpr LiveResourceSlot<LiveResourceSemantic::LinearColor> kOutput{"Test.Output", RHIFormat::RGBA16Float,
                                                                          LiveResourceExtent::Render};
    constexpr LiveResourceSlot<LiveResourceSemantic::Depth, RGResourceKind::Texture, LiveResourceCardinality::List>
        kDepths{"Test.Depths", RHIFormat::D32Float, LiveResourceExtent::Render, false, 3};

    void Require(bool condition, std::string_view message)
    {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error(std::string{message});
        }
    }

    bool SameHandle(RGHandle left, RGHandle right)
    {
        return left.index == right.index && left.version == right.version && left.kind == right.kind &&
               left.epoch == right.epoch;
    }

    LivePassNode Node(std::string name)
    {
        LivePassNode result;
        result.name = std::move(name);
        result.declare = [](LiveBlackboard&, EnhancedRenderGraph&, const EnhancedFrameContext&,
                            const LiveFrameBinding&) {};
        return result;
    }

    RGHandle Texture(EnhancedRenderGraph& graph, uint32_t width, uint32_t height,
                     RHIFormat format = RHIFormat::RGBA16Float)
    {
        RGTextureDesc descriptor;
        descriptor.width = width;
        descriptor.height = height;
        descriptor.format = format;
        descriptor.name = "feature-contract-probe";
        return graph.CreateTexture(descriptor);
    }

    LivePassNode Source()
    {
        LivePassNode result = Node("Source");
        result.Outputs({kInput});
        result.declare = [](LiveBlackboard& board, EnhancedRenderGraph& graph, const EnhancedFrameContext& context,
                            const LiveFrameBinding&)
        { board.Set(kInput, Texture(graph, context.width, context.height)); };
        return result;
    }

    void ExpectInvalid(const LiveResourceContract& producer, const LiveResourceContract& consumer,
                       std::string_view reason)
    {
        LivePipelineDesc pipeline;
        LivePassNode source = Node("Producer");
        source.Outputs({LiveResourceBinding{kInput.name, producer}});
        pipeline.AddNode(std::move(source));
        LivePassNode destination = Node("Consumer");
        destination.Inputs({LiveResourceBinding{kInput.name, consumer}});
        pipeline.AddNode(std::move(destination));
        std::string error;
        Require(!pipeline.Validate(error) && error.find("incompatible resource contract") != std::string::npos, reason);
    }

    void AssemblyContracts()
    {
        std::string selfTest;
        Require(LivePipelineDesc::RunSelfTest(selfTest), selfTest);
        LiveResourceContract contract = kInput.Contract();
        auto mismatch = contract;
        mismatch.semantic = LiveResourceSemantic::Normal;
        ExpectInvalid(contract, mismatch, "same-format normals and scene color must not connect");
        mismatch = contract;
        mismatch.format = RHIFormat::RGBA8Unorm;
        ExpectInvalid(contract, mismatch, "HDR and LDR contracts must not connect");
        mismatch = contract;
        mismatch.extent = LiveResourceExtent::Display;
        ExpectInvalid(contract, mismatch, "render extent must not promise display extent");
        mismatch = contract;
        mismatch.cardinality = LiveResourceCardinality::List;
        ExpectInvalid(contract, mismatch, "single and list resources must not connect");
        mismatch = contract;
        mismatch.kind = RGResourceKind::Buffer;
        mismatch.format = RHIFormat::Unknown;
        mismatch.extent = LiveResourceExtent::Independent;
        ExpectInvalid(contract, mismatch, "texture cannot satisfy a buffer contract");
        mismatch = contract;
        mismatch.optional = true;
        ExpectInvalid(mismatch, contract, "optional producer cannot satisfy a required consumer");
        auto list = kDepths.Contract();
        mismatch = list;
        mismatch.listSize = 2;
        ExpectInvalid(list, mismatch, "cascade list length mismatch must fail at assembly");

        LivePipelineDesc pipeline;
        pipeline.AddNode(Source());
        auto reader = Node("Reader");
        reader.Inputs({kInput});
        pipeline.AddNode(std::move(reader));
        auto modifier = Node("Modifier");
        modifier.Updates({kInput});
        pipeline.AddNode(std::move(modifier));
        auto sideEffect = Node("CPU.Dependency");
        sideEffect.dependsOn = {"Reader"};
        pipeline.AddNode(std::move(sideEffect));
        std::string error;
        Require(pipeline.Validate(error), error);
        const auto stats = pipeline.GetCompiledPlanStats();
        Require(stats.nodes == 4 && stats.resources == 1 && stats.dependencies == 4,
                "compiled plan must retain RAW, WAR and explicit CPU dependencies");
        const auto modifierDependencies = pipeline.GetNodeDependencies(2);
        Require(modifierDependencies.size() == 2 && modifierDependencies[0] == 0 && modifierDependencies[1] == 1,
                "modifier must wait for the previous writer and all prior readers");
        Require(pipeline.Validate(error) && pipeline.GetCompiledPlanStats().revision == stats.revision,
                "revalidation must reuse an installed immutable plan");
        pipeline.AddNode(Node("Extra"));
        Require(pipeline.GetCompiledPlanStats().revision == 0 && pipeline.Validate(error) &&
                    pipeline.GetCompiledPlanStats().revision != stats.revision,
                "appending a feature must invalidate the plan");
        pipeline.Clear();
        Require(pipeline.Validate(error) && pipeline.GetCompiledPlanStats().nodes == 0,
                "clearing a description must replace its compiled plan");

        auto conditional = Node("MissingFallback");
        conditional.Outputs({kOutput});
        conditional.active = [] { return false; };
        pipeline.AddNode(std::move(conditional));
        Require(!pipeline.Validate(error) && error.find("disabled passthrough") != std::string::npos,
                "disabled required output must declare a fallback");
        pipeline.Clear();
        pipeline.AddNode(Source());
        auto incompatible = Node("WrongFallback");
        const LiveResourceSlot<LiveResourceSemantic::Normal> normal{kOutput.name, RHIFormat::RGBA16Float,
                                                                    LiveResourceExtent::Render};
        incompatible.Outputs({normal});
        incompatible.active = [] { return false; };
        incompatible.disabledPassthroughs.push_back({normal, kInput});
        pipeline.AddNode(std::move(incompatible));
        Require(!pipeline.Validate(error) && error.find("incompatible disabled passthrough") != std::string::npos,
                "a disabled fallback must preserve semantics, format and extent");
        pipeline.Clear();
        auto missing = Node("MissingCPU");
        missing.dependsOn = {"Absent"};
        pipeline.AddNode(std::move(missing));
        Require(!pipeline.Validate(error), "missing CPU dependency must fail assembly");
        pipeline.Clear();
        pipeline.AddNode(Node("Duplicate"));
        pipeline.AddNode(Node("Duplicate"));
        Require(!pipeline.Validate(error), "node name collision across contributors must fail assembly");
    }

    void RuntimeContracts()
    {
        ce::regression::PortableRenderServices services;
        EnhancedRenderGraph graph{services, RGSchedulingMode::ExplicitVersioned};
        LiveBlackboard board;
        EnhancedFrameContext context;
        context.width = 65;
        context.height = 33;
        context.displayWidth = 130;
        context.displayHeight = 66;
        LiveFrameBinding binding;
        LivePipelineDesc pipeline;
        pipeline.AddNode(Source());
        int conditionalCalls{}, consumerCalls{};
        auto conditional = Node("OptionalFeature");
        conditional.Outputs({kOutput});
        conditional.active = [] { return false; };
        conditional.disabledPassthroughs.push_back({kOutput, kInput});
        conditional.declare = [&](LiveBlackboard&, EnhancedRenderGraph&, const EnhancedFrameContext&,
                                  const LiveFrameBinding&) { ++conditionalCalls; };
        pipeline.AddNode(std::move(conditional));
        auto consumer = Node("Consumer");
        consumer.Inputs({kOutput});
        consumer.declare =
            [&](LiveBlackboard& values, EnhancedRenderGraph&, const EnhancedFrameContext&, const LiveFrameBinding&)
        {
            Require(SameHandle(values.Get(kInput), values.Get(kOutput)),
                    "disabled feature must publish the actual input handle");
            ++consumerCalls;
        };
        pipeline.AddNode(std::move(consumer));
        std::string error;
        Require(pipeline.Validate(error), error);
        Require(pipeline.HasConsumer(kInput.name, context, binding),
                "an inactive passthrough must remain a consumer of its input producer");
        const auto compiledRevision = pipeline.GetCompiledPlanStats().revision;
        for (int frame = 0; frame < 3; ++frame)
        {
            graph.Reset();
            board.Reset();
            pipeline.DeclareAll(board, graph, context, binding);
            Require(board.Get(kOutput).epoch == graph.ResourceEpoch(),
                    "fallback must never retain an earlier graph epoch");
        }
        Require(conditionalCalls == 0 && consumerCalls == 3, "disabled producer must be bypassed for every frame");
        Require(pipeline.GetCompiledPlanStats().revision == compiledRevision,
                "frames must reuse one compiled feature plan");

        const auto rejectsPublished = [&](const LiveResourceBinding& output,
                                          std::function<void(LiveBlackboard&, EnhancedRenderGraph&)> publish,
                                          std::string_view expected)
        {
            graph.Reset();
            board.Reset();
            LivePipelineDesc invalid;
            auto producer = Node("InvalidProducer");
            producer.Outputs({output});
            producer.declare = [publish](LiveBlackboard& values, EnhancedRenderGraph& frame,
                                         const EnhancedFrameContext&, const LiveFrameBinding&)
            { publish(values, frame); };
            invalid.AddNode(std::move(producer));
            bool rejected{};
            try
            {
                invalid.DeclareAll(board, graph, context, binding);
            }
            catch (const std::runtime_error& failure)
            {
                rejected = std::string_view{failure.what()}.find(expected) != std::string_view::npos;
            }
            Require(rejected, expected);
        };
        rejectsPublished(
            kInput, [&](LiveBlackboard& values, EnhancedRenderGraph& frame)
            { values.Set(kInput, Texture(frame, context.width, context.height, RHIFormat::RGBA8Unorm)); },
            "texture format");
        rejectsPublished(
            kInput, [&](LiveBlackboard& values, EnhancedRenderGraph& frame)
            { values.Set(kInput, Texture(frame, context.displayWidth, context.displayHeight)); }, "render extent");
        rejectsPublished(
            kInput,
            [](LiveBlackboard& values, EnhancedRenderGraph& frame)
            {
                RGBufferDesc descriptor;
                descriptor.bytes = 64;
                values.Set(kInput, frame.CreateBuffer(descriptor));
            },
            "kind, epoch or version");
        rejectsPublished(kInput, [](LiveBlackboard&, EnhancedRenderGraph&) {}, "required resource is absent");
        const RGHandle stale = Texture(graph, context.width, context.height);
        rejectsPublished(
            kInput, [stale](LiveBlackboard& values, EnhancedRenderGraph&) { values.Set(kInput, stale); },
            "kind, epoch or version");
        rejectsPublished(
            kDepths,
            [&](LiveBlackboard& values, EnhancedRenderGraph& frame)
            {
                const RGHandle depth = Texture(frame, context.width, context.height, RHIFormat::D32Float);
                values.SetList(kDepths.name, {depth, depth});
            },
            "resource cardinality");
        const LiveResourceSlot<LiveResourceSemantic::AmbientOcclusion> half{"Test.Half", RHIFormat::RG16Float,
                                                                            LiveResourceExtent::HalfRender};
        graph.Reset();
        board.Reset();
        LivePipelineDesc halves;
        auto halfProducer = Node("HalfResolution");
        halfProducer.Outputs({half});
        halfProducer.declare = [half](LiveBlackboard& values, EnhancedRenderGraph& frame,
                                      const EnhancedFrameContext& input, const LiveFrameBinding&)
        { values.Set(half, Texture(frame, (input.width + 1) / 2, (input.height + 1) / 2, RHIFormat::RG16Float)); };
        halves.AddNode(std::move(halfProducer));
        halves.DeclareAll(board, graph, context, binding);
        Require(board.Get(half).IsValid(), "half render extent must round odd dimensions upward");

        LiveBlackboard copied{board};
        LiveBlackboard moved{std::move(board)};
        graph.Reset();
        moved.Reset();
        halves.DeclareAll(moved, graph, context, binding);
        Require(moved.Get(half).epoch == graph.ResourceEpoch(),
                "moving a bound board must preserve pointers to its moved storage");
        graph.Reset();
        board.Reset();
        halves.DeclareAll(board, graph, context, binding);
        Require(board.Get(half).epoch == graph.ResourceEpoch(),
                "a moved-from board must rebind before it can be reused");
        copied.Reset();
        graph.Reset();
        halves.DeclareAll(copied, graph, context, binding);
        Require(copied.Get(half).epoch == graph.ResourceEpoch(), "a copied board must bind its own slot storage");

        const auto previousRevision = halves.GetCompiledPlanStats().revision;
        halves = LivePipelineDesc{};
        auto replacement = half;
        replacement.name = "Test.Replacement";
        auto replacementProducer = Node("Replacement");
        replacementProducer.Outputs({replacement});
        replacementProducer.declare = [replacement](LiveBlackboard& values, EnhancedRenderGraph& frame,
                                                    const EnhancedFrameContext& input, const LiveFrameBinding&)
        {
            values.Set(replacement,
                       Texture(frame, (input.width + 1) / 2, (input.height + 1) / 2, RHIFormat::RG16Float));
        };
        halves.AddNode(std::move(replacementProducer));
        Require(halves.Validate(error) && halves.GetCompiledPlanStats().revision == previousRevision,
                "replacement fixture must reuse the same description address and authored revision");
        graph.Reset();
        board.Reset();
        halves.DeclareAll(board, graph, context, binding);
        Require(board.Get(replacement).IsValid() && !board.Get(half).IsValid(),
                "replacing a description must not reuse the old plan's blackboard slot order");
    }

    class TestPass final : public EnhancedRenderPass
    {
      public:
        uint32_t width{}, height{}, initialized{}, prepared{}, declared{}, stopped{};
        const char* GetName() const override { return "TestPass"; }
        bool Initialize(const EnhancedFrameContext&, std::string&) override
        {
            ++initialized;
            return true;
        }
        bool PrepareFrame(const EnhancedFrameContext& context, std::string&) override
        {
            width = context.width;
            height = context.height;
            ++prepared;
            return true;
        }
        void Declare(EnhancedRenderGraph&, const EnhancedFrameContext&) override { ++declared; }
        void Shutdown() override { ++stopped; }
    };

    void ViewIsolation()
    {
        std::array<TestPass, 3> passes;
        LivePipelineDesc pipeline;
        auto overlay = Node("PerViewOverlay");
        overlay.perView = true;
        overlay.requiredViewFlags = LiveViewFlags::kSceneOverlay;
        overlay.instance = [&](uint32_t index) -> EnhancedRenderPass* { return &passes.at(index); };
        auto optional = kOutput;
        optional.optional = true;
        overlay.Outputs({optional});
        overlay.declare = [&](LiveBlackboard& board, EnhancedRenderGraph& graph, const EnhancedFrameContext& context,
                              const LiveFrameBinding& binding)
        {
            auto& pass = passes.at(binding.viewIndex);
            pass.Declare(graph, context);
            board.Set(kOutput, Texture(graph, pass.width, pass.height));
        };
        pipeline.AddNode(std::move(overlay));
        EnhancedFrameContext first, second;
        first.width = 40;
        first.height = 20;
        first.viewFlags = LiveViewFlags::kSceneOverlay;
        second = first;
        second.width = 80;
        second.height = 60;
        std::string error;
        Require(pipeline.InitializeAll(first, 3, error), error);
        Require(pipeline.PrepareAll(first, 0, error) && pipeline.PrepareAll(second, 1, error), error);
        ce::regression::PortableRenderServices services;
        EnhancedRenderGraph firstGraph{services}, secondGraph{services};
        LiveBlackboard firstBoard, secondBoard;
        LiveFrameBinding firstBinding, secondBinding;
        firstBinding.viewFlags = secondBinding.viewFlags = LiveViewFlags::kSceneOverlay;
        secondBinding.viewIndex = 1;
        pipeline.DeclareAll(firstBoard, firstGraph, first, firstBinding);
        pipeline.DeclareAll(secondBoard, secondGraph, second, secondBinding);
        Require(passes[0].width == 40 && passes[1].width == 80 && passes[0].declared == 1 && passes[1].declared == 1,
                "preparing a second view must not overwrite the first view's retained pass inputs");
        EnhancedFrameContext game = first;
        game.viewFlags = 0;
        LiveFrameBinding gameBinding;
        gameBinding.viewIndex = 2;
        EnhancedRenderGraph gameGraph{services};
        LiveBlackboard gameBoard;
        Require(pipeline.PrepareAll(game, 2, error), error);
        pipeline.DeclareAll(gameBoard, gameGraph, game, gameBinding);
        Require(passes[2].initialized == 1 && passes[2].prepared == 0 && passes[2].declared == 0 &&
                    !gameBoard.Get(kOutput).IsValid(),
                "view flags must skip both preparation and declaration of optional view features");
        pipeline.ShutdownAll(3);
        Require(std::all_of(passes.begin(), passes.end(), [](const TestPass& pass) { return pass.stopped == 1; }),
                "all per-view lifecycle instances must shut down exactly once");
    }

    class TestContributor final : public IRenderFeatureContributor
    {
      public:
        std::string name;
        RenderFeatureStage stage;
        std::vector<std::string>* calls;
        uint32_t* destroyed;
        TestContributor(std::string value, RenderFeatureStage at, std::vector<std::string>& records, uint32_t& deaths)
            : name(std::move(value)), stage(at), calls(&records), destroyed(&deaths)
        {
        }
        ~TestContributor() override { ++*destroyed; }
        std::string_view GetName() const noexcept override { return name; }
        RenderFeatureStage GetStage() const noexcept override { return stage; }
        void Contribute(LivePipelineDesc& pipeline, const RenderFeatureContext&) override
        {
            calls->push_back(name);
            pipeline.AddNode(Node(name));
        }
    };

    class FunctionalContributor final : public IRenderFeatureContributor
    {
      public:
        std::string name;
        RenderFeatureStage stage;
        std::function<void(LivePipelineDesc&)> append;
        FunctionalContributor(std::string value, RenderFeatureStage at,
                              std::function<void(LivePipelineDesc&)> operation)
            : name(std::move(value)), stage(at), append(std::move(operation))
        {
        }
        std::string_view GetName() const noexcept override { return name; }
        RenderFeatureStage GetStage() const noexcept override { return stage; }
        void Contribute(LivePipelineDesc& pipeline, const RenderFeatureContext&) override { append(pipeline); }
    };

    void StageWriteContracts()
    {
        const auto base = []
        {
            LivePipelineDesc pipeline;
            auto source = Node("Engine.Source");
            source.Outputs({LiveResources::kLitColor, LiveResources::kDisplayLdr, LiveResources::kGBufferDepth,
                            LiveResources::kGBufferNormal});
            source.declare = [](LiveBlackboard& board, EnhancedRenderGraph& graph, const EnhancedFrameContext& context,
                                const LiveFrameBinding&)
            {
                board.Set(LiveResources::kLitColor, Texture(graph, context.width, context.height));
                board.Set(LiveResources::kDisplayLdr,
                          Texture(graph, context.width, context.height, RHIFormat::RGBA8Unorm));
                board.Set(LiveResources::kGBufferDepth,
                          Texture(graph, context.width, context.height, RHIFormat::D32Float));
                board.Set(LiveResources::kGBufferNormal, Texture(graph, context.width, context.height));
            };
            pipeline.AddNode(std::move(source));
            return pipeline;
        };
        const auto contribute = [](LivePipelineDesc& pipeline, RenderFeatureStage stage, LivePassNode node)
        {
            RenderFeatureContributors registry;
            auto feature = own::make_shared<FunctionalContributor>(
                "Test.Feature", stage,
                [node = std::move(node)](LivePipelineDesc& destination) { destination.AddNode(node); });
            std::string error;
            Require(registry.Add(std::move(feature), error), error);
            registry.Contribute(stage, pipeline, {});
        };
        const auto reject = [&](RenderFeatureStage stage, LiveResourceBinding slot, bool publishes)
        {
            auto pipeline = base();
            auto illegal = Node("AttemptedReplacement");
            if (publishes)
            {
                illegal.Outputs({slot});
            }
            else
            {
                illegal.Updates({slot});
            }
            contribute(pipeline, stage, std::move(illegal));
            std::string error;
            Require(!pipeline.Validate(error) && error.find("Test.Feature") != std::string::npos &&
                        error.find(RenderFeatureStageName(stage)) != std::string::npos &&
                        error.find("AttemptedReplacement") != std::string::npos &&
                        error.find(slot.name) != std::string::npos &&
                        error.find("read-only built-in") != std::string::npos,
                    "stage write rejection must identify contributor, stage, node and slot");
        };
        reject(RenderFeatureStage::AfterOpaque, LiveResources::kGBufferDepth, false);
        reject(RenderFeatureStage::BeforePostProcess, LiveResources::kGBufferDepth, true);
        reject(RenderFeatureStage::BeforePresent, LiveResources::kLitColor, false);
        reject(RenderFeatureStage::AfterOpaque, LiveResources::kTemporalMotion, false);
        reject(RenderFeatureStage::BeforePostProcess, LiveResources::kMeteorLayerCommands, false);

        auto pipeline = base();
        constexpr LiveResourceSlot<LiveResourceSemantic::LinearColor> privateColor{
            "Test.PrivateColor", RHIFormat::RGBA16Float, LiveResourceExtent::Render};
        auto opaque = Node("Private.Producer");
        opaque.Inputs({LiveResources::kGBufferDepth, LiveResources::kGBufferNormal});
        opaque.Updates({LiveResources::kLitColor});
        opaque.Outputs({privateColor});
        opaque.declare = [privateColor](LiveBlackboard& board, EnhancedRenderGraph& graph,
                                        const EnhancedFrameContext& context, const LiveFrameBinding&)
        {
            board.Set(LiveResources::kLitColor, board.Get(LiveResources::kLitColor));
            board.Set(privateColor, Texture(graph, context.width, context.height));
        };
        contribute(pipeline, RenderFeatureStage::AfterOpaque, std::move(opaque));
        auto post = Node("Private.HDR");
        post.Updates({LiveResources::kLitColor, privateColor});
        contribute(pipeline, RenderFeatureStage::BeforePostProcess, std::move(post));
        auto display = Node("Private.Display");
        display.Inputs({privateColor});
        display.Updates({LiveResources::kDisplayLdr, privateColor});
        display.declare = [privateColor](LiveBlackboard& board, EnhancedRenderGraph&, const EnhancedFrameContext&,
                                         const LiveFrameBinding&)
        {
            board.Set(LiveResources::kDisplayLdr, board.Get(LiveResources::kDisplayLdr));
            board.Set(privateColor, board.Get(privateColor));
        };
        contribute(pipeline, RenderFeatureStage::BeforePresent, std::move(display));
        std::string error;
        Require(pipeline.Validate(error), error);
        ce::regression::PortableRenderServices services;
        EnhancedRenderGraph graph{services, RGSchedulingMode::ExplicitVersioned};
        LiveBlackboard board;
        EnhancedFrameContext context;
        context.width = 32;
        context.height = 16;
        pipeline.DeclareAll(board, graph, context, {});
        Require(board.Get(privateColor).IsValid() && board.Get(LiveResources::kDisplayLdr).IsValid(),
                "all stages must allow their color output and dependency-checked private outputs/updates");

        auto undeclared = base();
        auto sneaky = Node("UndeclaredDepthWrite");
        sneaky.Inputs({LiveResources::kGBufferDepth});
        sneaky.Updates({LiveResources::kLitColor});
        sneaky.declare =
            [](LiveBlackboard& values, EnhancedRenderGraph&, const EnhancedFrameContext&, const LiveFrameBinding&)
        { values.Set(LiveResources::kGBufferDepth, values.Get(LiveResources::kGBufferDepth)); };
        contribute(undeclared, RenderFeatureStage::AfterOpaque, std::move(sneaky));
        Require(undeclared.Validate(error), error);
        graph.Reset();
        board.Reset();
        bool denied{};
        try
        {
            undeclared.DeclareAll(board, graph, context, {});
        }
        catch (const std::runtime_error& failure)
        {
            denied = std::string_view{failure.what()}.find("wrote undeclared slot 'GBuffer.Depth'") !=
                     std::string_view::npos;
        }
        Require(denied, "a callback must not evade stage permissions by omitting its blackboard write declaration");
        board.Set("Test.AfterFailure", {});
        Require(!board.Get("Test.AfterFailure").IsValid(), "exception cleanup must close the feature write scope");

        auto untyped = base();
        auto legacy = Node("UntypedContribution");
        legacy.modifies = {LiveSlots::kLitColor};
        contribute(untyped, RenderFeatureStage::AfterOpaque, std::move(legacy));
        Require(!untyped.Validate(error) && error.find("requires typed") != std::string::npos,
                "contributors must use typed resource contracts at their public boundary");
    }

    void ProgramBindingIntegration()
    {
        ce::regression::PortableRenderServices services;
        EnhancedRenderGraph graph{services, RGSchedulingMode::ExplicitVersioned};
        LiveBlackboard board;
        EnhancedFrameContext context;
        context.width = 32;
        context.height = 16;
        uint32_t frame{}, executed{};
        LivePipelineDesc pipeline;
        auto source = Source();
        source.declare = [&](LiveBlackboard& values, EnhancedRenderGraph& target, const EnhancedFrameContext& current,
                             const LiveFrameBinding&)
        {
            const RGHandle output = target.Write(Texture(target, current.width, current.height));
            target.AddPass(
                "Feature.ProgramOutput", {{output, RHIResourceState::RenderTarget, RGAccessMode::Write}},
                [&, captured = frame](const auto&) { executed = captured; }, true);
            values.Set(kInput, output);
        };
        pipeline.AddNode(std::move(source));
        std::string error;
        own::shared_owner<const EnhancedRenderGraph::GraphProgram> program;
        for (frame = 1; frame <= 2; ++frame)
        {
            graph.Reset();
            board.Reset();
            graph.BeginProgramBinding();
            pipeline.DeclareAll(board, graph, context, {});
            Require(graph.FinishProgramBinding(error) && graph.Compile(error), error);
            Require(graph.Execute(error) && executed == frame,
                    "feature declarations must retain the current callback in program-binding mode");
            if (frame == 1)
            {
                program = graph.SnapshotProgram();
                Require(bool(program), "cold feature declaration must publish a compiled graph program");
            }
            else
            {
                Require(graph.GetStats().programBindingReused &&
                            graph.SnapshotProgram().operator->() == program.operator->(),
                        "feature contracts must coexist with retained graph usage storage and compiled program reuse");
            }
        }
    }

    bool AllowWarmAllocations()
    {
        const char* value = std::getenv("CE_LIVE_CONTRACT_ALLOW_ALLOCATIONS");
        return value && std::string_view{value} == "1";
    }

    void ReportAllocations(std::string_view phase, const ce::regression::AllocationCounts& counts, uint32_t frames,
                           uint32_t items)
    {
        std::cout << "{\"measurement\":\"" << phase << "\",\"frames\":" << frames << ",\"items\":" << items
                  << ",\"standardCalls\":" << counts.standardCalls << ",\"standardBytes\":" << counts.standardBytes
                  << ",\"mimallocCalls\":" << counts.mimallocCalls << ",\"mimallocBytes\":" << counts.mimallocBytes
                  << ",\"native_or_gpu\":false}\n";
        Require(AllowWarmAllocations() || (!counts.standardCalls && !counts.mimallocCalls),
                "warmed feature dispatch or light selection allocated temporary storage");
    }

    void WarmDeclarationAllocations()
    {
        ce::regression::PortableRenderServices services;
        EnhancedRenderGraph graph{services, RGSchedulingMode::ExplicitVersioned};
        LiveBlackboard board;
        EnhancedFrameContext context;
        context.width = 32;
        context.height = 16;
        LivePipelineDesc pipeline;
        RGHandle input;
        std::array<RGHandle, 3> depths{};
        auto source = Node("Warm.Source");
        source.Outputs({kInput, kDepths});
        source.declare =
            [&](LiveBlackboard& values, EnhancedRenderGraph&, const EnhancedFrameContext&, const LiveFrameBinding&)
        {
            values.Set(kInput, input);
            values.SetList(kDepths, depths);
        };
        pipeline.AddNode(std::move(source));
        bool enabled{};
        for (uint32_t index = 0; index < 32; ++index)
        {
            auto update = Node("Warm.Update." + std::to_string(index));
            update.Inputs({kDepths});
            update.Updates({kInput});
            update.active = [&] { return enabled; };
            update.declare =
                [](LiveBlackboard& values, EnhancedRenderGraph&, const EnhancedFrameContext&, const LiveFrameBinding&)
            {
                values.Set(kInput, values.Get(kInput));
                (void)values.GetListView(kDepths);
            };
            pipeline.AddNode(std::move(update));
        }
        auto passthrough = Node("Warm.Disabled");
        passthrough.Outputs({kOutput});
        passthrough.active = [] { return false; };
        passthrough.disabledPassthroughs.push_back({kOutput, kInput});
        pipeline.AddNode(std::move(passthrough));
        std::string error;
        Require(pipeline.Validate(error), error);
        uint64_t a{}, b{}, c{}, d{}, e{}, f{};
        // Six references exceed std::function's small-buffer storage. This is
        // the same owning conversion the live diagnostic observer used to make.
        const auto observer = [&a, &b, &c, &d, &e, &f](const LivePassNode&, const LiveBlackboard&)
        {
            ++a;
            ++b;
            ++c;
            ++d;
            ++e;
            ++f;
        };
        constexpr uint32_t warmup = 4, measuredFrames = 128;
        for (const bool observe : {false, true})
        {
            ce::regression::AllocationCounts counts;
            for (uint32_t frame = 0; frame < warmup + measuredFrames; ++frame)
            {
                enabled = (frame & 1) != 0;
                graph.Reset();
                board.Reset();
                graph.BeginProgramBinding();
                input = graph.Write(Texture(graph, context.width, context.height));
                for (auto& depth : depths)
                {
                    depth = graph.Write(Texture(graph, context.width, context.height, RHIFormat::D32Float));
                }
                graph.AddPass(
                    "Warm.Output",
                    {{input, RHIResourceState::RenderTarget, RGAccessMode::Write},
                     {depths[0], RHIResourceState::DepthWrite, RGAccessMode::Write},
                     {depths[1], RHIResourceState::DepthWrite, RGAccessMode::Write},
                     {depths[2], RHIResourceState::DepthWrite, RGAccessMode::Write}},
                    [](const auto&) {}, true);
                const auto declare = [&]
                {
                    pipeline.PrepareAll(context, 0, error);
                    (void)pipeline.HasConsumer(kInput.name, context, {});
                    if (observe)
                    {
                        pipeline.DeclareAll(board, graph, context, {}, observer);
                    }
                    else
                    {
                        pipeline.DeclareAll(board, graph, context, {});
                    }
                };
                if (frame < warmup)
                {
                    declare();
                }
                else
                {
                    // Graph resource creation/compilation is measured by its
                    // own probe. This scope isolates actual feature dispatch,
                    // validation, blackboard reuse and observer conversion.
                    const ce::regression::ScopedAllocationMeasurement measurement{counts};
                    declare();
                }
                Require(SameHandle(board.Get(kOutput), input) && board.GetListView(kDepths).size() == 3,
                        "warm feature dispatch lost its disabled passthrough or list slots");
                Require(graph.FinishProgramBinding(error) && graph.Compile(error) && graph.Execute(error), error);
                if (frame)
                {
                    Require(graph.GetStats().programBindingReused,
                            "repeated feature measurements must retain the actual graph program");
                }
            }
            ReportAllocations(observe ? "warm_feature_dispatch_observer" : "warm_feature_dispatch", counts,
                              measuredFrames, static_cast<uint32_t>(pipeline.NodeCount()));
        }
        Require(a != 0 && a == b && b == c && c == d && d == e && e == f,
                "borrowed observer lost one of its captured references");
    }

    void WarmLightSelectionAllocations()
    {
        std::vector<own::shared_owner<LightRenderProxy>> proxies;
        for (uint32_t index = 0; index < 256; ++index)
        {
            auto light = own::make_shared<LightRenderProxy>();
            light->m_lightType = index % 11 == 0 ? LightType::DirectionalLight : LightType::PointLight;
            light->m_worldPosition = {float(index % 32), 0.f, float(index / 32)};
            light->m_intensity = float(index % 7 + 1);
            proxies.push_back(std::move(light));
        }
        FrameCameraSnapshot camera;
        camera.isOrthographic = true;
        ViewLightSelection selected;
#if !defined(CE_FEATURE_ALLOCATION_LEGACY_LIGHT_API)
        ViewLightSelectionScratch scratch;
#endif
        constexpr uint32_t warmup = 4, measuredFrames = 128;
        ce::regression::AllocationCounts counts;
        for (uint32_t frame = 0; frame < warmup + measuredFrames; ++frame)
        {
            const auto select = [&]
            {
#if defined(CE_FEATURE_ALLOCATION_LEGACY_LIGHT_API)
                selected = SelectLightsForView(proxies, camera);
#else
                SelectLightsForView(proxies, camera, selected, scratch);
#endif
            };
            if (frame < warmup)
            {
                select();
            }
            else
            {
                const ce::regression::ScopedAllocationMeasurement measurement{counts};
                select();
            }
            Require(selected.sceneLights == proxies.size() && selected.lights.size() == proxies.size() &&
                        selected.culledByFrustum == 0,
                    "warm light selection lost enabled lights");
        }
        ReportAllocations("warm_light_selection", counts, measuredFrames, static_cast<uint32_t>(proxies.size()));
    }

#if !defined(CE_FEATURE_ALLOCATION_LEGACY_LIGHT_API)
    void LightSelectionBehavior()
    {
        std::vector<own::shared_owner<LightRenderProxy>> proxies;
        for (uint32_t index = 0; index < 8; ++index)
        {
            auto light = own::make_shared<LightRenderProxy>();
            light->m_lightType = LightType::PointLight;
            light->m_worldPosition = {float(index), 0.f, 5.f};
            light->m_range = 100.f;
            light->m_intensity = 4.f;
            light->m_color = {.2f, .3f, .4f, .5f};
            proxies.push_back(std::move(light));
        }
        proxies[1]->m_lightType = LightType::DirectionalLight;
        proxies[1]->m_intensity = 2.f;
        proxies[3]->m_lightType = LightType::DirectionalLight;
        proxies[3]->m_intensity = 5.f;
        proxies[4]->m_intensity = 7.f;
        proxies[5]->m_lightType = LightType::SpotLight;
        proxies[6]->m_range = 0.f;
        proxies[7]->m_lightStatus = LightStatus::Disabled;
        proxies.push_back({});
        FrameCameraSnapshot camera;
        camera.isOrthographic = true;
        ViewLightSelection selected;
        ViewLightSelectionScratch scratch;
        SelectLightsForView(proxies, camera, selected, scratch);
        Require(selected.sceneLights == 7 && selected.culledByFrustum == 1 && selected.lights.size() == 6,
                "light selection must preserve disabled/null/range culling counts");
        constexpr std::array<uint32_t, 6> expected{3, 1, 4, 0, 2, 5};
        for (size_t index = 0; index < expected.size(); ++index)
        {
            const auto& light = selected.lights[index];
            const auto& source = *proxies[expected[index]];
            Require(light.position.x == float(expected[index]) && light.color.r == source.m_color.r &&
                        light.color.g == source.m_color.g && light.color.b == source.m_color.b &&
                        light.color.a == source.m_intensity,
                    "sorted shader lights must preserve finite equal-score order and unmultiplied RGB/intensity");
        }
        const auto oneShot = SelectLightsForView(proxies, camera);
        Require(oneShot.lights.size() == selected.lights.size() && oneShot.sceneLights == selected.sceneLights &&
                    oneShot.culledByFrustum == selected.culledByFrustum,
                "one-shot light selection must preserve its public returning API");

        // Non-finite authored intensity is legal input to the proxy. A total
        // tie break keeps sort's comparator strict even when scores are NaN.
        proxies[0]->m_intensity = std::numeric_limits<float>::quiet_NaN();
        proxies[1]->m_intensity = std::numeric_limits<float>::quiet_NaN();
        proxies[2]->m_intensity = std::numeric_limits<float>::infinity();
        proxies[3]->m_intensity = -std::numeric_limits<float>::infinity();
        proxies[4]->m_intensity = 0.f;
        proxies[5]->m_intensity = -0.f;
        SelectLightsForView(proxies, camera, selected, scratch);
        constexpr std::array<uint32_t, 6> nonFiniteOrder{3, 1, 2, 4, 5, 0};
        for (size_t index = 0; index < nonFiniteOrder.size(); ++index)
        {
            Require(selected.lights[index].position.x == float(nonFiniteOrder[index]),
                    "NaN/infinity/signed-zero scores must retain deterministic strict ordering");
        }

        camera.isOrthographic = false;
        camera.projection = math::perspective_fov_lh(1.f, 1.f, .1f, 100.f);
        camera.inverseView = math::matrix4x4::identity();
        proxies[0]->m_intensity = 1.f;
        proxies[0]->m_range = .5f;
        proxies[0]->m_worldPosition = {0.f, 0.f, 5.f};
        proxies[2]->m_range = .5f;
        proxies[2]->m_worldPosition = {1000.f, 0.f, 5.f};
        SelectLightsForView(std::span{proxies}.first(3), camera, selected, scratch);
        Require(selected.sceneLights == 3 && selected.culledByFrustum == 1 && selected.lights.size() == 2 &&
                    selected.lights[0].position.w == float(LightType::DirectionalLight) &&
                    selected.lights[1].position.z == 5.f,
                "perspective frustum selection must keep infinite lights and cull local lights outside the view");
        const auto outputCapacity = selected.lights.capacity();
        const auto scratchCapacity = scratch.scored.capacity();
        ce::regression::AllocationCounts counts;
        {
            const ce::regression::ScopedAllocationMeasurement measurement{counts};
            SelectLightsForView({}, camera, selected, scratch);
        }
        Require(selected.lights.empty() && !selected.sceneLights && !selected.culledByFrustum &&
                    selected.lights.capacity() == outputCapacity && scratch.scored.capacity() == scratchCapacity &&
                    !counts.standardCalls && !counts.mimallocCalls,
                "empty/smaller views must reset results while retaining both output and scoring capacity");
    }

    void ObserverCompatibility()
    {
        ce::regression::PortableRenderServices services;
        EnhancedRenderGraph graph{services, RGSchedulingMode::ExplicitVersioned};
        LiveBlackboard board;
        LivePipelineDesc pipeline;
        pipeline.AddNode(Node("Observer.Source"));
        const auto invoke = [&](auto&& callback)
        {
            graph.Reset();
            board.Reset();
            graph.BeginProgramBinding();
            pipeline.DeclareAll(board, graph, {}, {}, std::forward<decltype(callback)>(callback));
        };
        uint32_t calls{};
        std::function<void(const LivePassNode&, const LiveBlackboard&)> owned = [&](const auto&, const auto&)
        { ++calls; };
        invoke(owned);
        invoke(std::as_const(owned));
        invoke(std::function<void(const LivePassNode&, const LiveBlackboard&)>{});
        using CallbackPointer = void (*)(const LivePassNode&, const LiveBlackboard&);
        invoke(CallbackPointer{});
        Require(calls == 2, "owning std::function and empty/null callback compatibility must remain unchanged");
        auto mutableCallback = [count = 0u, &calls](const auto&, const auto&) mutable { calls += ++count; };
        invoke(mutableCallback);
        invoke(mutableCallback);
        Require(calls == 5, "borrowed mutable lvalue callback must retain its own current state");
        invoke([owner = std::make_unique<uint32_t>(7), &calls](const auto&, const auto&) { calls += *owner; });
        Require(calls == 12, "move-only temporary callback must remain alive through synchronous declaration");
    }
#endif

    void ContributorComposition()
    {
        std::vector<std::string> calls;
        uint32_t destroyed{};
        RenderFeatureContributors registry;
        std::string error;
        Require(!registry.Add({}, error), "null contributor owner must be rejected");
        auto first =
            own::make_shared<TestContributor>("Opaque.First", RenderFeatureStage::AfterOpaque, calls, destroyed);
        auto last =
            own::make_shared<TestContributor>("Display.Last", RenderFeatureStage::BeforePresent, calls, destroyed);
        auto second =
            own::make_shared<TestContributor>("Opaque.Second", RenderFeatureStage::AfterOpaque, calls, destroyed);
        auto middle =
            own::make_shared<TestContributor>("Post.Middle", RenderFeatureStage::BeforePostProcess, calls, destroyed);
        Require(registry.Add(first, error) && registry.Add(last, error) && registry.Add(second, error) &&
                    registry.Add(middle, error),
                "multiple owned contributors must register");
        Require(!registry.Add(first, error) && error.find("already registered") != std::string::npos,
                "duplicate contributor identity must be rejected");
        LivePipelineDesc pipeline;
        RenderFeatureContext context;
        registry.Contribute(RenderFeatureStage::AfterOpaque, pipeline, context);
        registry.Contribute(RenderFeatureStage::BeforePostProcess, pipeline, context);
        registry.Contribute(RenderFeatureStage::BeforePresent, pipeline, context);
        Require(calls == std::vector<std::string>{"Opaque.First", "Opaque.Second", "Post.Middle", "Display.Last"},
                "stage order and registration order within a stage must be deterministic");
        Require(pipeline.Validate(error), error);
        RenderFeatureContributors installed = registry;
        first.reset();
        last.reset();
        second.reset();
        middle.reset();
        Require(registry.Remove("Opaque.First") && !registry.Remove("Absent") && registry.Size() == 3,
                "removal must match the registered stable identity");
        registry.Clear();
        Require(destroyed == 0 && installed.Size() == 4,
                "installed snapshots must retain contributor ownership after registry edits");
        installed.Clear();
        Require(destroyed == 4, "the last registry snapshot must release all contributors");
    }
} // namespace

int main()
{
    try
    {
        AssemblyContracts();
        RuntimeContracts();
        ViewIsolation();
        ContributorComposition();
        StageWriteContracts();
        ProgramBindingIntegration();
        WarmDeclarationAllocations();
        WarmLightSelectionAllocations();
#if !defined(CE_FEATURE_ALLOCATION_LEGACY_LIGHT_API)
        LightSelectionBehavior();
        ObserverCompatibility();
#endif
        std::cout << "LIVE_PIPELINE_CONTRACTS_OK checks=" << checks << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "LIVE_PIPELINE_CONTRACTS_FAILED checks=" << checks << " error=" << error.what() << '\n';
        return 1;
    }
}
