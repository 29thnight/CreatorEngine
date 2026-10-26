// material_mesh_surface_probe.cpp의 익명 namespace 안에서 포함한다.
void VerifyNullTextureBindings(const std::filesystem::path& requestedRoot)
{
    constexpr std::uint32_t kSlotCount = 4096;
    constexpr std::uint32_t kTableCount = 2;
    constexpr std::uint32_t kRecordingCount = 2;
    constexpr std::uint32_t kDeviceLifetimes = 2;
    constexpr std::uint64_t kOutputBytes = kSlotCount * sizeof(std::array<float, 4>);
    static_assert(BindlessTextureCapacity == kSlotCount);

    const auto root = std::filesystem::absolute(requestedRoot);
    auto* paths = (&ce::paths::get_state());
    paths->base_project_path = root / "Dynamic_CPP";
    paths->shader_source_path = root / "Dynamic_CPP/Assets/Shaders";
    paths->asset_authoring_enabled = true;
    const auto shaderFile = root / "Build/Obj/MaterialProductProbe/null-texture-bindings.slang";
    std::filesystem::create_directories(shaderFile.parent_path());
    {
        std::ofstream shader(shaderFile, std::ios::binary | std::ios::trunc);
        Check(bool(shader), "Null texture probe shader file");
        shader << R"slang(
Texture2D<float4> inputs[4096] : register(t0);
RWStructuredBuffer<float4> results : register(u0);
cbuffer ProbeFrame : register(b0)
{
    uint frameOrdinal;
};

[numthreads(64, 1, 1)]
void ReadNullTextureBindings(uint3 dispatchId : SV_DispatchThreadID)
{
    uint index = dispatchId.x;
    if (index >= 4096)
    {
        return;
    }
    float marker = float(index + 1 + frameOrdinal * 4096);
    results[index] = inputs[NonUniformResourceIndex(index)].Load(int3(0, 0, 0)) + marker;
}
)slang";
        shader.close();
        Check(bool(shader), "Null texture probe shader write");
    }
    RHIShaderBlob bytecode;
    {
        RHIShaderCompiler::ScopedOutput output(RHIShaderBinary::Dxil);
        std::string error;
        const bool compiled = RHIShaderCompiler::CompileFile(
            shaderFile.string(), "ReadNullTextureBindings", "cs_6_0", bytecode, error);
        Check(compiled, "Null texture probe shader compile: " + error);
    }

    // 두 표의 서로 다른 실제 텍스처 위치가 255/256/257개 null 구간을 만든다.
    // 마지막 두 슬롯도 서로 바꿔 같은 목적지에 덮어 쓴 표를 GPU 값으로 구별한다.
    const auto isRealTexture = [](std::uint32_t pattern, std::uint32_t slot)
    {
        if (pattern == 0)
        {
            return slot == 255 || slot == 512 || slot == 770 || slot == 4095;
        }
        return slot == 0 || slot == 256 || slot == 513 || slot == 771 || slot == 4094;
    };
    const auto firstComponent = gpuComponents;
    DX12DeviceResources device;
    for (std::uint32_t lifetime = 0; lifetime < kDeviceLifetimes; ++lifetime)
    {
        {
            struct NativeOwners
            {
                DX12DeviceResources& device;
                DX12RootSignatureCache roots{};
                DX12PSOManager pipelines{};
                RHITextureHandle source{};
                std::array<RHIBufferHandle, kTableCount> outputs{};
                std::array<RHIReadback, kTableCount> readbacks{};
                bool ready{};

                ~NativeOwners()
                {
                    // 예외에서도 GPU 참조가 끝난 뒤 버퍼·PSO·레이아웃을 해제한다.
                    // 제출 실패가 idle/device-loss 증명까지 막으면 소유자를 먼저 풀지 않는다.
                    // native device만 생긴 초기화 실패는 제출 client가 준비되지 않았을 수 있다.
                    if (ready)
                    {
                        if (device.GetCurrentUploadRecordingId())
                        {
                            device.AbortFrame();
                        }
                        std::string error;
                        if (!device.DrainForLifecycle(RHILifecycleCommand::OfflineReadbackCapture, error))
                        {
                            std::fprintf(stderr, "Null texture probe cleanup could not prove GPU idle: %s\n",
                                         error.c_str());
                            std::terminate();
                        }
                    }
                    for (auto& readback : readbacks)
                    {
                        if (readback.buffer.IsValid())
                        {
                            device.ReleaseReadback(readback);
                        }
                    }
                    for (const auto buffer : outputs)
                    {
                        if (buffer.IsValid())
                        {
                            device.ReleaseBuffer(buffer);
                        }
                    }
                    if (source.IsValid())
                    {
                        device.ReleaseTexture(source);
                    }
                    pipelines.Shutdown();
                    roots.Shutdown();
                    device.Shutdown();
                }
            } owners{device};
            std::string error;
            const auto require = [&](bool condition, const char* message)
            {
                Check(condition, std::string(message) + ": " + error);
            };
            require(device.Initialize(16, 16, error), "Null texture device initialize");
            owners.ready = true;
            Check(device.HasDebugMessageQueue(), "Null texture probe requires DX12 validation messages");
            require(owners.roots.Initialize(&device, error), "Null texture root cache");
            require(owners.pipelines.Initialize(&device, L"", error), "Null texture pipeline cache");
            const std::array parameters{
                RHILayout::SrvTable(kSlotCount), RHILayout::Uav(0), RHILayout::Cbv(0)};
            const auto layout = owners.roots.GetOrCreate({parameters, {}, false}, error);
            require(layout.IsValid(), "Null texture layout");
            RHIComputePipelineDesc pipelineDescription;
            pipelineDescription.layout = layout;
            pipelineDescription.csBytecode = bytecode.Data();
            pipelineDescription.csSize = bytecode.Size();
            const auto pipeline = owners.pipelines.GetOrCreateCompute(pipelineDescription, error);
            require(pipeline.IsValid(), "Null texture compute pipeline");

            const std::array<float, 4> color = lifetime == 0
                ? std::array<float, 4>{1, 0, 1, 1} : std::array<float, 4>{0, 1, 0, 1};
            RHITextureDesc sourceDescription;
            sourceDescription.width = sourceDescription.height = 1;
            sourceDescription.format = RHIFormat::RGBA8Unorm;
            sourceDescription.allowRenderTarget = true;
            sourceDescription.debugName = L"Probe.NullTexture.Source";
            std::copy(color.begin(), color.end(), sourceDescription.clearColor);
            require(device.CreateTexture(sourceDescription, owners.source, error), "Null texture source");
            RHIBufferDesc outputDescription;
            outputDescription.bytes = kOutputBytes;
            outputDescription.allowUnorderedAccess = true;
            outputDescription.debugName = L"Probe.NullTexture.Results";
            for (std::uint32_t table = 0; table < kTableCount; ++table)
            {
                require(device.CreateBuffer(outputDescription, owners.outputs[table], error),
                        "Null texture result buffer");
                require(device.CreateBufferReadback(kOutputBytes, owners.readbacks[table], error),
                        "Null texture readback");
            }

            RHIResourceState sourceState = RHIResourceState::Common;
            std::uint64_t previousRecording{}, previousVersion{};
            for (std::uint32_t recording = 0; recording < kRecordingCount; ++recording)
            {
                require(device.BeginFrame(error), "Null texture recording begin");
                const auto recordingId = device.GetCurrentUploadRecordingId();
                const auto version = device.GetDescriptorVersionToken();
                Check(recordingId != 0 && version != 0 &&
                          recordingId != previousRecording && version != previousVersion,
                      "Null texture new recording and descriptor version");
                if (previousVersion != 0)
                {
                    Check(!device.IsDescriptorVersionCurrent(previousVersion),
                          "Null texture previous table version is no longer current");
                }
                const std::array<std::uint32_t, 4> constants{
                    lifetime * kRecordingCount + recording + 1, 0, 0, 0};
                const auto upload = device.UploadConstants(constants.data(), sizeof(constants));
                Check(upload.IsValid(), "Null texture frame marker upload");
                std::array<std::vector<RHIBindingDesc>, kTableCount> descriptions;
                for (std::uint32_t table = 0; table < kTableCount; ++table)
                {
                    auto& values = descriptions[table];
                    values.assign(kSlotCount, RHIBindingDesc::Srv2D({}, RHIFormat::RGBA8Unorm).OrNull());
                    for (std::uint32_t slot = 0; slot < kSlotCount; ++slot)
                    {
                        if (isRealTexture(table ^ recording, slot))
                        {
                            values[slot] = RHIBindingDesc::Srv2D(owners.source, RHIFormat::RGBA8Unorm);
                        }
                    }
                    // 형식/밉이 다른 null은 같은 Texture2D 셰이더로 읽되 기존 생성 경로에 남긴다.
                    values[1023] = RHIBindingDesc::Srv2D({}, RHIFormat::RGBA32Float).OrNull();
                    values[1024] = RHIBindingDesc::Srv2D({}, RHIFormat::RGBA8Unorm, 1, 1).OrNull();
                }
                std::array<RHIBindingTable, kTableCount> tables{};
                {
                    std::promise<void> start;
                    const auto gate = start.get_future().share();
                    std::array<std::future<RHIBindingTable>, kTableCount> pending;
                    try
                    {
                        for (std::uint32_t table = 0; table < kTableCount; ++table)
                        {
                            pending[table] = std::async(std::launch::async, [&, gate, table]
                            {
                                gate.wait();
                                return device.CreateBindings(descriptions[table]);
                            });
                        }
                    }
                    catch (...)
                    {
                        // 두 번째 worker 생성이 실패해도 첫 worker를 풀고 future가 먼저 합류한다.
                        start.set_value();
                        throw;
                    }
                    start.set_value();
                    for (std::uint32_t table = 0; table < kTableCount; ++table)
                    {
                        tables[table] = pending[table].get();
                    }
                }
                Check(device.GetCurrentUploadRecordingId() == recordingId &&
                          device.GetDescriptorVersionToken() == version,
                      "Null texture parallel allocation preserves its prepared recording");
                for (const auto& table : tables)
                {
                    Check(table.IsValid() && table.backend != 0 && table.count == kSlotCount &&
                              table.version == version && device.IsDescriptorVersionCurrent(table.version),
                          "Null texture full table identity and current version");
                }
                Check(tables[0].backend != tables[1].backend,
                      "Null texture concurrent tables own different destination ranges");
                const std::array colorTargets{owners.source};
                const auto target = device.CreateRenderTargets(colorTargets);
                Check(target.IsValid(), "Null texture source render target");

                // 모든 descriptor worker가 끝난 뒤 현재 encoder를 얻고 자원 참조를 기록한다.
                auto& encoder = device.GetImmediateEncoder();
                const RHITransition toClear{owners.source, sourceState, RHIResourceState::RenderTarget};
                encoder.ResourceBarriers({{&toClear, 1}});
                encoder.ClearRenderTargets(target, color.data());
                const RHITransition toRead{
                    owners.source, RHIResourceState::RenderTarget, RHIResourceState::ShaderResource};
                encoder.ResourceBarriers({{&toRead, 1}});
                sourceState = RHIResourceState::ShaderResource;
                for (std::uint32_t table = 0; table < kTableCount; ++table)
                {
                    const RHIBufferTransition toWrite{
                        owners.outputs[table], RHIResourceState::Common, RHIResourceState::UnorderedAccess};
                    encoder.ResourceBarriers({{}, {&toWrite, 1}});
                    encoder.SetPipeline(RHIBindPoint::Compute, pipeline);
                    encoder.SetBindings(RHIBindPoint::Compute, 0, tables[table]);
                    Check(encoder.SetRootUnorderedBuffer(
                              RHIBindPoint::Compute, 1, {owners.outputs[table], 0, kOutputBytes}),
                          "Null texture result root UAV");
                    encoder.SetConstantBuffer(RHIBindPoint::Compute, 2, upload);
                    encoder.Dispatch(kSlotCount / 64, 1, 1);
                    const RHIBufferTransition toCopy{
                        owners.outputs[table], RHIResourceState::UnorderedAccess, RHIResourceState::CopySource};
                    encoder.ResourceBarriers({{}, {&toCopy, 1}});
                    encoder.CopyBufferToReadback(owners.readbacks[table], owners.outputs[table]);
                    const RHIBufferTransition toCommon{
                        owners.outputs[table], RHIResourceState::CopySource, RHIResourceState::Common};
                    encoder.ResourceBarriers({{}, {&toCommon, 1}});
                }
                require(device.EndFrame(error), "Null texture recording submit");
                require(GetRHISubmissionThread().DrainSubmissions(&device, error), "Null texture native submit");
                require(device.DrainForLifecycle(RHILifecycleCommand::OfflineReadbackCapture, error),
                        "Null texture GPU completion");
                for (std::uint32_t table = 0; table < kTableCount; ++table)
                {
                    RHIReadbackImage image;
                    require(device.MapReadback(owners.readbacks[table], image, error), "Null texture map");
                    Check(image.data.size() == kOutputBytes, "Null texture complete readback size");
                    const auto* actual = image.Elements<std::array<float, 4>>();
                    for (std::uint32_t slot = 0; slot < kSlotCount; ++slot)
                    {
                        // 슬롯/프레임 표식을 빼서 미기록 값이나 이전 프레임의 0을 성공으로 보지 않는다.
                        const float marker = float(slot + 1 + constants[0] * kSlotCount);
                        for (unsigned component = 0; component < 4; ++component)
                        {
                            const float expected = isRealTexture(table ^ recording, slot) ? color[component] : 0.f;
                            Near(actual[slot][component] - marker, expected,
                                 "Null texture slot " + std::to_string(slot) + " table " + std::to_string(table), 0.0);
                        }
                    }
                }
                std::string messages;
                const auto messageCount = device.DrainDebugMessages(messages);
                Check(messageCount == 0, "Null texture GPU validation: " + messages);
                previousRecording = recordingId;
                previousVersion = version;
            }
        }
        Check(!device.IsInitialized(), "Null texture device lifetime shutdown");
    }
    std::cout << "NULL_TEXTURE_BINDINGS_OK compiled=1 deviceLifetimes=2 recordings=4 tables=8 slots=32768 "
                 "parallelAllocators=2 nullRunBoundaries=255,256,257 endSlot=4095 alternateNullViews=2 gpuComponents="
              << gpuComponents - firstComponent << " validation=0\n";
}
