void RunSkinLodError(RecordingChangeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines,
                     const std::filesystem::path& root)
{
    using namespace experiment::importer;
    auto fixture = MakeGeometry(false, false);
    const auto& geometry = fixture.draw.modelMeshView;
    const auto mask = geometry.vertexAttributeMask | assets::kSkinVertexAttributes;
    const auto stride = assets::StrideOf(mask);
    constexpr uint32_t side = 9;
    std::vector<std::byte> bytes(side * side * stride);
    experiment::Mesh mesh;
    for (uint32_t y = 0; y < side; ++y)
    {
        for (uint32_t x = 0; x < side; ++x)
        {
            const auto vertex = y * side + x;
            auto* data = bytes.data() + vertex * stride;
            for (const auto& attribute : assets::kVertexAttributeTable)
            {
                if (assets::Has(geometry.vertexAttributeMask, attribute.attribute))
                {
                    std::memcpy(data + assets::OffsetOf(mask, attribute.attribute),
                                fixture.vertices.data() +
                                    assets::OffsetOf(geometry.vertexAttributeMask, attribute.attribute),
                                assets::SizeOf(attribute.format));
                }
            }
            const float position[3]{-.6f + x * .15f, -.6f + y * .15f, .07f * std::sin(float(x)) * std::cos(float(y))};
            const uint32_t indices = 0x100u;
            float weights[4]{float(x) / 8, 1.f - float(x) / 8, 0, 0};
            if (vertex == 0)
            {
                std::memset(weights, 0, sizeof(weights));
            }
            std::memcpy(data, position, 12);
            std::memcpy(data + assets::OffsetOf(mask, assets::VertexAttribute::BoneIndices), &indices, 4);
            std::memcpy(data + assets::OffsetOf(mask, assets::VertexAttribute::BoneWeights), weights, 16);
            if (x + 1 < side && y + 1 < side)
            {
                const uint32_t a = vertex, b = a + 1, c = a + side, d = c + 1;
                mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
            }
        }
    }
    Check(mesh.vertices.AssignPacked(mask, side * side, bytes), "Skin error packed fixture");
    mesh.bounds = math::aabb::from_min_max({-.6f, -.6f, -.1f}, {.6f, .6f, .1f});
    std::string error;
    Check(BuildMeshlets(mesh, mesh.meshlets, error), "Skin error meshlets " + error);
    experiment::MeshLodBuildSettings settings;
    settings.levelCount = 2;
    settings.targetRelativeError = .5f;
    Check(BuildMeshLods(mesh, mesh.coarseLods, error, settings) && !mesh.coarseLods.IsEmpty(),
          "Skinned replacement cooking " + error);
    const auto& group = mesh.coarseLods.groups.back();
    const auto& coarse = mesh.coarseLods.levels[group.lodIndex - 1].indices;
    Check(coarse.size() < mesh.indices.size(), "Skinned root actually removes triangles");
    auto corrupt = mesh.coarseLods;
    corrupt.groups.back().reserved[0] = 0;
    corrupt.geometryDigest = ComputeMeshLodGeometryDigest(mesh, corrupt);
    Check(!ValidateMeshLods(mesh, corrupt, error), "Reject underestimated skin error despite a matching digest");
    corrupt = mesh.coarseLods;
    corrupt.groups.back().reserved[2] = 2;
    corrupt.geometryDigest = ComputeMeshLodGeometryDigest(mesh, corrupt);
    Check(!ValidateMeshLods(mesh, corrupt, error), "Reject an unknown skin certificate tag");
    const auto vertices = SkinErrorVertices(mesh);
    const RHIPipelineLayoutParam params[]{RHILayout::Srv(0), RHILayout::Srv(1), RHILayout::UavBufferTable(1, 0)};
    const auto layout = roots.GetOrCreate({params, {}}, error);
    Check(layout.IsValid(), "Skin bound layout " + error);
    LX::Runtime::CompiledCompute shader;
    RHIShaderCompileOptions options;
    options.strictMath = true;
    Check(LX::Runtime::CompileCompute((root / "Tools/regression/support/SkinLodErrorProbe.slang").string(),
                                      "SkinLodErrorCS", {}, options, shader, error),
          "Skin bound shader " + error);
    LX::Runtime::ComputePipeline kernel;
    RHIComputePipelineDesc desc;
    desc.layout = layout;
    desc.csBytecode = shader.stage.bytecode.Data();
    desc.csSize = shader.stage.bytecode.Size();
    Check(kernel.Create(pipelines, desc, std::move(shader.description), error), "Skin bound PSO " + error);
    uint64_t samples{};
    double maximumRatio{};
    for (unsigned poseIndex = 0; poseIndex < 6; ++poseIndex)
    {
        std::array<math::matrix4x4, 2> bones{math::matrix4x4::identity(), math::matrix4x4::identity()};
        if (poseIndex == 1)
        {
            for (auto& bone : bones)
            {
                bone.m[3][0] = .4f;
                bone.m[3][1] = -.2f;
            }
        }
        if (poseIndex == 2)
        {
            bones[0].m[0][0] = 0;
            bones[0].m[0][1] = 1;
            bones[0].m[1][0] = -1;
            bones[0].m[1][1] = 0;
            bones[1].m[3][0] = .5f;
        }
        if (poseIndex == 3)
        {
            bones[0].m[0][0] = 2;
            bones[0].m[1][1] = .3f;
            bones[1].m[0][1] = .7f;
            bones[1].m[2][2] = 1.8f;
        }
        if (poseIndex == 4)
        {
            bones[0].m[0][0] = -1;
            bones[1].m[3][2] = 2;
        }
        if (poseIndex == 5)
        {
            bones[0].m[3][0] = 100;
            bones[1].m[3][0] = -100;
            bones[1].m[1][1] = .01f;
        }
        std::vector<SkinErrorVector> posed(vertices.size());
        for (size_t i = 0; i < vertices.size(); ++i)
        {
            const auto& v = vertices[i];
            if (v.joints[0] == 256)
            {
                posed[i] = v.position;
                continue;
            }
            for (unsigned influence = 0; influence < 4; ++influence)
            {
                if (v.weights[influence] == 0)
                {
                    continue;
                }
                const auto& bone = bones[v.joints[influence]];
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    double value = bone.m[3][axis];
                    for (unsigned k = 0; k < 3; ++k)
                    {
                        value += v.position[k] * bone.m[k][axis];
                    }
                    posed[i][axis] += v.weights[influence] * value;
                }
            }
        }
        double actual{};
        const auto distance = [&](auto from, auto to)
        {
            for (size_t triangle = 0; triangle < from.size(); triangle += 3)
            {
                for (unsigned u = 0; u <= 4; ++u)
                {
                    for (unsigned v = 0; u + v <= 4; ++v)
                    {
                        SkinErrorVector point{};
                        const double bary[3]{double(u) / 4, double(v) / 4, 1 - double(u + v) / 4};
                        for (unsigned k = 0; k < 3; ++k)
                        {
                            for (unsigned c = 0; c < 3; ++c)
                            {
                                point[c] += bary[k] * posed[from[triangle + k]][c];
                            }
                        }
                        double nearest = INFINITY;
                        for (size_t target = 0; target < to.size(); target += 3)
                        {
                            const auto weights = SkinErrorClosestBarycentric(
                                point, posed[to[target]], posed[to[target + 1]], posed[to[target + 2]]);
                            SkinErrorVector delta = point;
                            for (unsigned k = 0; k < 3; ++k)
                            {
                                for (unsigned c = 0; c < 3; ++c)
                                {
                                    delta[c] -= weights[k] * posed[to[target + k]][c];
                                }
                            }
                            nearest = std::min(nearest, SkinErrorDot(delta, delta));
                        }
                        actual = std::max(actual, std::sqrt(nearest));
                        ++samples;
                    }
                }
            }
        };
        distance(mesh.indices, coarse);
        distance(coarse, mesh.indices);
        Check(device.BeginFrame(error), "Skin bound frame");
        std::vector<std::byte> pose(128);
        const uint32_t boneCount = 2;
        std::memcpy(pose.data(), &boneCount, 4);
        for (unsigned bone = 0; bone < 2; ++bone)
        {
            const auto packed = PackedBoneMatrix::From(bones[bone]);
            std::memcpy(pose.data() + 32 + bone * 48, &packed, 48);
        }
        const auto upload = [&](const void* data, uint64_t size)
        {
            const auto slice = device.AllocateUpload({size, RHIUploadUsage::Raw, 256});
            Check(slice.IsWritable(), "Skin certificate upload");
            std::memcpy(slice.cpuAddress, data, size);
            return slice;
        };
        const auto gpuPose = upload(pose.data(), pose.size()), gpuGroup = upload(&group, sizeof(group));
        RHIBufferHandle output;
        RHIBufferDesc buffer;
        buffer.bytes = 32;
        buffer.allowUnorderedAccess = true;
        Check(device.CreateBuffer(buffer, output, error), "Skin metric output");
        const RHIBindingDesc binding = RHIBindingDesc::UavBuffer(output, 2, 16);
        const auto table = device.CreateBindings({&binding, 1});
        auto& encoder = device.GetImmediateEncoder();
        const RHIBufferTransition before{output, RHIResourceState::Common, RHIResourceState::UnorderedAccess};
        encoder.ResourceBarriers({{}, {&before, 1}});
        encoder.SetPipeline(RHIBindPoint::Compute, kernel.GetGeneration()->GetHandle());
        encoder.SetRootBuffer(RHIBindPoint::Compute, 0, gpuPose);
        encoder.SetRootBuffer(RHIBindPoint::Compute, 1, gpuGroup);
        encoder.SetBindings(RHIBindPoint::Compute, 2, table);
        encoder.Dispatch(1, 1, 1);
        const RHIBufferTransition after{output, RHIResourceState::UnorderedAccess, RHIResourceState::CopySource};
        encoder.ResourceBarriers({{}, {&after, 1}});
        RHIReadback readback;
        Check(device.CreateBufferReadback(32, readback, error), "Skin metric readback");
        encoder.CopyBufferToReadback(readback, output);
        Check(device.EndFrame(error) && device.DrainForLifecycle(RHILifecycleCommand::OfflineReadbackCapture, error),
              "Skin metric completion " + error);
        RHIReadbackImage image;
        Check(device.MapReadback(readback, image, error), "Skin metric map");
        float result[8];
        std::memcpy(result, image.data.data(), 32);
        Check(result[5] == 1 && result[4] >= actual - 1e-5, "GPU error encloses independently posed surface samples");
        for (const auto& point : posed)
        {
            const SkinErrorVector center{result[0], result[1], result[2]};
            const auto delta = SkinErrorSubtract(point, center);
            Check(std::sqrt(SkinErrorDot(delta, delta)) <= result[3] + 1e-4,
                  "GPU posed group bound encloses source and coarse vertices");
        }
        maximumRatio = std::max(maximumRatio, actual / (std::max)(double(result[4]), 1e-20));
        device.ReleaseReadback(readback);
        device.ReleaseBuffer(output);
    }
    std::cout << "METEOR_SKIN_LOD_BOUND_OK poses=6 malformed=2 samples=" << samples << " maxErrorRatio=" << maximumRatio
              << " validation=0\n";
}
