#include "Assets/ClusterTopologyPage.h"
#include "Assets/ModelCpuGeometry.h"
#include <stdint.h>
#include "../../Engine/Utility_Framework/StandardLibrary.h"

int main()
{
    using namespace assets;
    try
    {
        const auto check = [](bool valid, const char* message) { if (!valid) throw std::runtime_error(message); };
        constexpr uint32_t count = 4;
        const auto mask = kCoreVertexAttributes;
        const auto stride = StrideOf(mask);
        std::vector<std::byte> raw(count * stride);
        for (uint32_t vertex = 0; vertex < count; ++vertex)
        {
            for (uint32_t channel = 0; channel < stride / 4; ++channel)
                quantized::Put(raw, vertex * stride + channel * 4, std::bit_cast<uint32_t>(float(vertex + channel) / 16));
            const float normal[3]{0,0,1}, tangent[4]{1,0,0,1};
            std::memcpy(raw.data() + vertex * stride + OffsetOf(mask, VertexAttribute::Normal), normal, sizeof(normal));
            std::memcpy(raw.data() + vertex * stride + OffsetOf(mask, VertexAttribute::Tangent), tangent, sizeof(tangent));
        }
        std::string error;
        // Wrap a real QMC1 packet, retaining exact attribute words in this fixture.
        cluster::Grid grid;
        grid.origin.resize(stride / 4); grid.step.resize(stride / 4);
        const auto packet = cluster::Encode(mask, count, raw, grid);
        experiment::MeshletPayload topology;
        topology.vertexRemap = {7, 2, 5, 1};
        topology.triangleIndices = {0, 2, 1, 1, 2, 3};
        topology.primitiveRemap = {4, 9};
        experiment::MeshletDescriptor descriptor;
        descriptor.vertexCount = count; descriptor.triangleCount = 2; descriptor.sphereRadius = 2;
        const auto encoded = cluster::EncodeTopologyPage(packet, topology, descriptor, 8, 10);
        const auto view = cluster::InspectTopologyPage(encoded, 8, 10);
        cluster::ValidateTopologyPageAttributes(encoded, mask, 8, 10, 0, descriptor, 0);
        std::vector<std::byte> decoded;
        check(cluster::Decode(view.attributes, decoded, error) && decoded == raw, "Exact page attribute roundtrip");
        for (uint32_t vertex = 0; vertex < count; ++vertex)
            check(quantized::Word(encoded, view.sourceOffset + vertex * 4) == topology.vertexRemap[vertex], "Source ID order");
        for (uint32_t triangle = 0; triangle < 2; ++triangle)
            check(quantized::Word(encoded, view.primitiveOffset + triangle * 4) == topology.primitiveRemap[triangle], "Primitive ID order");
        for (uint32_t corner = 0; corner < 6; ++corner)
            check(std::to_integer<uint32_t>(encoded[view.triangleOffset + corner]) == topology.triangleIndices[corner], "Exact winding");
        uint32_t rejected{};
        const std::array<uint32_t, 12> corruptOffsets{0,4,8,12,16,20,24,28,32,44,view.sourceOffset,view.primitiveOffset};
        for (const auto offset : corruptOffsets)
        {
            auto bad = encoded;
            quantized::Put(bad, offset, UINT32_MAX);
            try { cluster::InspectTopologyPage(bad, 8, 10); }
            catch (const std::exception&) { ++rejected; }
        }
        auto bad = encoded; bad[view.triangleOffset] = std::byte{4};
        try { cluster::InspectTopologyPage(bad, 8, 10); }
        catch (const std::exception&) { ++rejected; }
        for (std::size_t size = 0; size < encoded.size(); ++size)
        {
            bool failed{};
            try { cluster::InspectTopologyPage(std::span(encoded).first(size), 8, 10); }
            catch (const std::exception&) { failed = true; }
            check(failed, "Truncated page accepted");
        }
        check(rejected == corruptOffsets.size() + 1, "Malformed page accepted");
        uint32_t semanticRejected{};
        for (uint32_t mutation = 0; mutation < 3; ++mutation)
        {
            auto values = raw;
            if (mutation == 0)
                for (uint32_t component = 0; component < 3; ++component)
                    quantized::Put(values, OffsetOf(mask, VertexAttribute::Normal) + component * 4, 0);
            if (mutation == 1) quantized::Put(values, OffsetOf(mask, VertexAttribute::Tangent) + 12, std::bit_cast<uint32_t>(.5f));
            if (mutation == 2) quantized::Put(values, 0, std::bit_cast<uint32_t>(10.f));
            const auto data = cluster::EncodeTopologyPage(cluster::Encode(mask, count, values, grid), topology, descriptor, 8, 10);
            try { cluster::ValidateTopologyPageAttributes(data, mask, 8, 10, 0, descriptor, 0); }
            catch (const std::exception&) { ++semanticRejected; }
        }
        {
            const auto skinMask = kV2VertexAttributes, skinStride = StrideOf(skinMask);
            std::vector<std::byte> values(count * skinStride);
            for (uint32_t vertex = 0; vertex < count; ++vertex)
            {
                std::memcpy(values.data() + vertex * skinStride, raw.data() + vertex * stride, stride);
                quantized::Put(values, vertex * skinStride + OffsetOf(skinMask, VertexAttribute::BoneIndices), 0xffffff00u);
                quantized::Put(values, vertex * skinStride + OffsetOf(skinMask, VertexAttribute::BoneWeights), std::bit_cast<uint32_t>(1.f));
            }
            cluster::Grid skinGrid; skinGrid.origin.resize(skinStride / 4); skinGrid.step.resize(skinStride / 4);
            const auto page = [&](const auto& bytes) { return cluster::EncodeTopologyPage(cluster::Encode(skinMask, count, bytes, skinGrid), topology, descriptor, 8, 10); };
            cluster::ValidateTopologyPageAttributes(page(values), skinMask, 8, 10, 1, descriptor, 0);
            for (uint32_t mutation = 0; mutation < 2; ++mutation)
            {
                auto badValues = values;
                if (mutation == 0) quantized::Put(badValues, OffsetOf(skinMask, VertexAttribute::BoneIndices), 0xffffff01u);
                else quantized::Put(badValues, OffsetOf(skinMask, VertexAttribute::BoneWeights), std::bit_cast<uint32_t>(.5f));
                try { cluster::ValidateTopologyPageAttributes(page(badValues), skinMask, 8, 10, 1, descriptor, 0); }
                catch (const std::exception&) { ++semanticRejected; }
            }
        }
        check(semanticRejected == 5, "Invalid attributes/bounds/skin were admitted");
        const std::vector<uint32_t> exactIndices{0,1,2,0,2,3};
        auto exact = raw;
        quantized::Put(exact,0,std::bit_cast<uint32_t>(.1234567f));
        quantized::Put(exact,stride,std::bit_cast<uint32_t>(-0.f));
        const auto cpu = cpu_geometry::Encode(mask,count,exact,exactIndices);
        cpu_geometry::Geometry decodedCpu; std::string cpuError;
        check(cpu_geometry::Decode(cpu,decodedCpu,cpuError) && decodedCpu.indices == exactIndices,"Exact CPU artifact decode");
        for (uint32_t vertex = 0; vertex < count; ++vertex)
            check(std::memcmp(exact.data()+vertex*stride,&decodedCpu.positions[vertex],12) == 0,"CPU positions must preserve original float bits");
        size_t cpuRejected{};
        for (size_t end = 0; end < cpu.size(); ++end)
        {
            auto sentinel = decodedCpu;
            if (!cpu_geometry::Decode(std::span(cpu).first(end),sentinel,cpuError) && sentinel.indices == exactIndices) ++cpuRejected;
        }
        check(cpuRejected == cpu.size(),"Truncated CPU artifact must reject transactionally");
        for (uint32_t offset : {0u,4u,8u,12u,16u,20u,24u,static_cast<uint32_t>(cpu.size()-4)})
        {
            auto bad = cpu; quantized::Put(bad,offset,UINT32_MAX);
            check(!cpu_geometry::Decode(bad,decodedCpu,cpuError),"Malformed CPU artifact accepted");
        }
        std::cout << "CPU_GEOMETRY_CODEC_OK exactPositions=4 signedZero=1 exactIndices=6 truncations=" << cpuRejected << " malformed=8\n";
        std::cout << "CLUSTER_TOPOLOGY_PAGE_OK vertices=4 triangles=2 malformed=" << rejected
                  << " truncations=" << encoded.size() << " semantic=" << semanticRejected << " exactAttributes=1 exactConnectivity=1\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
