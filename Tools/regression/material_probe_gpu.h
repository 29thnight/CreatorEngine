#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <algorithm>
#include <cstring>
#include <limits>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace MaterialProbe
{
using Microsoft::WRL::ComPtr;

struct Float4
{
    float x, y, z, w;
};
static_assert(sizeof(Float4) == 16);

struct TextureFixture
{
    unsigned width = 0;
    unsigned height = 0;
    std::vector<Float4> pixels;
    bool srgb8 = false;
};

struct SamplerFixture
{
    bool linear = true;
    bool repeat = true;
};

struct BindingFixture
{
    unsigned textureRegister = 0;
    unsigned samplerRegister = 0;
    unsigned space = 1;
    std::vector<std::uint8_t> uniforms;
};

inline void Require(HRESULT result, const char* operation)
{
    if (FAILED(result))
    {
        std::ostringstream message;
        message << operation << " failed: 0x" << std::hex << result;
        throw std::runtime_error(message.str());
    }
}

class ComputeReadback final
{
  public:
    template<typename Input>
    std::vector<Float4> Run(const std::vector<uint8_t>& code, const std::vector<Input>& inputsData, unsigned angleCount,
                            unsigned fieldCount, const std::vector<TextureFixture>& texturesData = {},
                            const std::vector<SamplerFixture>& samplersData = {}, const BindingFixture& bindings = {})
    {
        ComPtr<IDXGIFactory4> factory;
        Require(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");
        ComPtr<ID3D12Device> device;
        bool warp = false;
        if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device))))
        {
            ComPtr<IDXGIAdapter> adapter;
            Require(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "EnumWarpAdapter");
            Require(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)),
                    "D3D12CreateDevice WARP");
            warp = true;
        }
        ComPtr<IDXGIAdapter1> selected;
        Require(factory->EnumAdapterByLuid(device->GetAdapterLuid(), IID_PPV_ARGS(&selected)), "EnumAdapterByLuid");
        DXGI_ADAPTER_DESC1 adapterDescription{};
        Require(selected->GetDesc1(&adapterDescription), "GetDesc1");
        std::wcout << L"ADAPTER " << adapterDescription.Description << (warp ? L" (WARP)\n" : L" (hardware)\n");

        D3D12_ROOT_PARAMETER parameters[4]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        D3D12_DESCRIPTOR_RANGE textureRange{};
        textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        textureRange.NumDescriptors = static_cast<UINT>(texturesData.size());
        textureRange.RegisterSpace = bindings.space;
        textureRange.BaseShaderRegister = bindings.textureRegister;
        parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[2].DescriptorTable = {1, &textureRange};
        std::vector<D3D12_STATIC_SAMPLER_DESC> samplers;
        for (UINT index = 0; index < samplersData.size(); ++index)
        {
            const auto& data = samplersData[index];
            D3D12_STATIC_SAMPLER_DESC sampler{};
            sampler.Filter = data.linear ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
            sampler.AddressU = sampler.AddressV = sampler.AddressW =
                data.repeat ? D3D12_TEXTURE_ADDRESS_MODE_WRAP : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            sampler.MaxLOD = std::numeric_limits<float>::max();
            sampler.ShaderRegister = bindings.samplerRegister + index;
            sampler.RegisterSpace = bindings.space;
            samplers.push_back(sampler);
        }
        D3D12_ROOT_SIGNATURE_DESC rootDescription{};
        rootDescription.NumParameters = texturesData.empty() ? 2 : 3;
        const auto uniformRoot = rootDescription.NumParameters;
        if (!bindings.uniforms.empty())
        {
            parameters[uniformRoot].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            parameters[uniformRoot].Descriptor.ShaderRegister = 2;
            ++rootDescription.NumParameters;
        }
        rootDescription.pParameters = parameters;
        rootDescription.NumStaticSamplers = static_cast<UINT>(samplers.size());
        rootDescription.pStaticSamplers = samplers.data();
        ComPtr<ID3DBlob> rootBlob;
        ComPtr<ID3DBlob> errors;
        Require(D3D12SerializeRootSignature(&rootDescription, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &errors),
                "D3D12SerializeRootSignature");
        ComPtr<ID3D12RootSignature> root;
        Require(device->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(),
                                            IID_PPV_ARGS(&root)),
                "CreateRootSignature");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDescription{};
        pipelineDescription.pRootSignature = root.Get();
        pipelineDescription.CS = {code.data(), code.size()};
        ComPtr<ID3D12PipelineState> pipeline;
        Require(device->CreateComputePipelineState(&pipelineDescription, IID_PPV_ARGS(&pipeline)),
                "CreateComputePipelineState");

        const auto buffer = [&device](uint64_t bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state,
                                      D3D12_RESOURCE_FLAGS flags) {
            D3D12_HEAP_PROPERTIES heapProperties{};
            heapProperties.Type = heap;
            D3D12_RESOURCE_DESC description{};
            description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            description.Width = bytes;
            description.Height = 1;
            description.DepthOrArraySize = 1;
            description.MipLevels = 1;
            description.SampleDesc.Count = 1;
            description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            description.Flags = flags;
            ComPtr<ID3D12Resource> resource;
            Require(device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &description, state, nullptr,
                                                    IID_PPV_ARGS(&resource)),
                    "CreateCommittedResource");
            return resource;
        };
        const size_t inputBytes = inputsData.size() * sizeof(Input);
        const size_t outputCount = inputsData.size() * angleCount * fieldCount;
        const size_t outputBytes = outputCount * sizeof(Float4);
        auto inputs =
            buffer(inputBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
        auto outputs = buffer(outputBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        auto readback =
            buffer(outputBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE);
        ComPtr<ID3D12Resource> uniforms;
        if (!bindings.uniforms.empty())
        {
            const auto size = (bindings.uniforms.size() + 255u) & ~std::size_t(255u);
            uniforms =
                buffer(size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
            void* mapped = nullptr;
            Require(uniforms->Map(0, nullptr, &mapped), "Map material uniforms");
            std::memset(mapped, 0, size);
            std::memcpy(mapped, bindings.uniforms.data(), bindings.uniforms.size());
            uniforms->Unmap(0, nullptr);
        }
        Input* mappedInputs = nullptr;
        const D3D12_RANGE noRead{0, 0};
        Require(inputs->Map(0, &noRead, reinterpret_cast<void**>(&mappedInputs)), "Map inputs");
        for (size_t index = 0; index < inputsData.size(); ++index)
        {
            mappedInputs[index] = inputsData[index];
        }
        inputs->Unmap(0, nullptr);

        D3D12_COMMAND_QUEUE_DESC queueDescription{};
        queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ComPtr<ID3D12CommandQueue> queue;
        Require(device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&queue)), "CreateCommandQueue");
        ComPtr<ID3D12CommandAllocator> allocator;
        Require(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
                "CreateCommandAllocator");
        ComPtr<ID3D12GraphicsCommandList> commands;
        Require(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), pipeline.Get(),
                                          IID_PPV_ARGS(&commands)),
                "CreateCommandList");
        commands->SetComputeRootSignature(root.Get());
        commands->SetComputeRootShaderResourceView(0, inputs->GetGPUVirtualAddress());
        commands->SetComputeRootUnorderedAccessView(1, outputs->GetGPUVirtualAddress());
        if (uniforms)
            commands->SetComputeRootConstantBufferView(uniformRoot, uniforms->GetGPUVirtualAddress());
        std::vector<ComPtr<ID3D12Resource>> textures, textureUploads;
        ComPtr<ID3D12DescriptorHeap> textureHeap;
        if (!texturesData.empty())
        {
            D3D12_DESCRIPTOR_HEAP_DESC heap{};
            heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            heap.NumDescriptors = static_cast<UINT>(texturesData.size());
            heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            Require(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&textureHeap)), "Create texture descriptor heap");
            auto handle = textureHeap->GetCPUDescriptorHandleForHeapStart();
            for (const auto& data : texturesData)
            {
                if (!data.width || !data.height || data.pixels.size() != std::size_t(data.width) * data.height)
                    throw std::runtime_error("Invalid texture fixture extent");
                D3D12_RESOURCE_DESC description{};
                description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                description.Width = data.width;
                description.Height = data.height;
                description.DepthOrArraySize = description.MipLevels = 1;
                description.Format = data.srgb8 ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R32G32B32A32_FLOAT;
                description.SampleDesc.Count = 1;
                D3D12_HEAP_PROPERTIES textureProperties{};
                textureProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
                ComPtr<ID3D12Resource> texture;
                Require(device->CreateCommittedResource(&textureProperties, D3D12_HEAP_FLAG_NONE, &description,
                                                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                        IID_PPV_ARGS(&texture)),
                        "Create fixture texture");
                D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
                UINT64 uploadSize = 0;
                device->GetCopyableFootprints(&description, 0, 1, 0, &footprint, nullptr, nullptr, &uploadSize);
                auto upload = buffer(uploadSize, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                                     D3D12_RESOURCE_FLAG_NONE);
                void* mappedTexture = nullptr;
                Require(upload->Map(0, &noRead, &mappedTexture), "Map fixture texture");
                for (unsigned row = 0; row < data.height; ++row)
                {
                    auto* destination = static_cast<std::uint8_t*>(mappedTexture) + footprint.Offset +
                                        row * footprint.Footprint.RowPitch;
                    if (!data.srgb8)
                        std::memcpy(destination, data.pixels.data() + std::size_t(row) * data.width,
                                    data.width * sizeof(Float4));
                    else
                        for (unsigned column = 0; column < data.width; ++column)
                        {
                            const auto& pixel = data.pixels[std::size_t(row) * data.width + column];
                            const float values[]{pixel.x, pixel.y, pixel.z, pixel.w};
                            for (unsigned component = 0; component < 4; ++component)
                                destination[column * 4 + component] = static_cast<std::uint8_t>(
                                    (std::clamp)(values[component], 0.0f, 1.0f) * 255.0f + 0.5f);
                        }
                }
                upload->Unmap(0, nullptr);
                D3D12_TEXTURE_COPY_LOCATION source{}, target{};
                source.pResource = upload.Get();
                source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                source.PlacedFootprint = footprint;
                target.pResource = texture.Get();
                target.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                commands->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
                D3D12_RESOURCE_BARRIER textureBarrier{};
                textureBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                textureBarrier.Transition.pResource = texture.Get();
                textureBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                textureBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                textureBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                commands->ResourceBarrier(1, &textureBarrier);
                D3D12_SHADER_RESOURCE_VIEW_DESC view{};
                view.Format = description.Format;
                view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                view.Texture2D.MipLevels = 1;
                device->CreateShaderResourceView(texture.Get(), &view, handle);
                handle.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                textures.push_back(std::move(texture));
                textureUploads.push_back(std::move(upload));
            }
            ID3D12DescriptorHeap* heaps[]{textureHeap.Get()};
            commands->SetDescriptorHeaps(1, heaps);
            commands->SetComputeRootDescriptorTable(2, textureHeap->GetGPUDescriptorHandleForHeapStart());
        }
        commands->Dispatch(static_cast<UINT>(inputsData.size()), angleCount, 1);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = outputs.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commands->ResourceBarrier(1, &barrier);
        commands->CopyResource(readback.Get(), outputs.Get());
        Require(commands->Close(), "Close command list");
        ID3D12CommandList* lists[]{commands.Get()};
        queue->ExecuteCommandLists(1, lists);
        ComPtr<ID3D12Fence> fence;
        Require(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence");
        Require(queue->Signal(fence.Get(), 1), "Signal");
        m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!m_event)
        {
            throw std::runtime_error("CreateEvent failed");
        }
        Require(fence->SetEventOnCompletion(1, m_event), "SetEventOnCompletion");
        if (WaitForSingleObject(m_event, 30000) != WAIT_OBJECT_0)
        {
            throw std::runtime_error("Compute readback did not complete in 30 seconds");
        }
        void* mappedData = nullptr;
        const D3D12_RANGE resultRange{0, outputBytes};
        Require(readback->Map(0, &resultRange, &mappedData), "Map readback");
        const auto* mappedResults = static_cast<const Float4*>(mappedData);
        std::vector<Float4> result(mappedResults, mappedResults + outputCount);
        readback->Unmap(0, &noRead);
        return result;
    }

    ~ComputeReadback()
    {
        if (m_event)
        {
            CloseHandle(m_event);
        }
    }

  private:
    HANDLE m_event{};
};
} // namespace MaterialProbe
