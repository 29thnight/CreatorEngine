#pragma once
#include "PSO.h"
#ifndef DYNAMICCPP_EXPORTS
// d3d11shader.h 는 셰이더 blob 리플렉션(D3DReflect)용이다 — 디바이스를 쓰지
// 않는다. SM5.1 이하에서는 DX12도 같은 인터페이스로 blob을 읽는다.
#include <d3d11shader.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <string_view>
#include <cstdint>
#include <unordered_map>
#include <span>
#include <cstddef>

//-----------------------------------------------------------------------------
// ShaderPSO: 셰이더 자산의 파라미터 레이아웃 + CPU 값 (M1, 2026-08-08)
//
// ★ DX11 절반을 걷어냈다. 여기 있던 것: 상수 버퍼의 ID3D11Buffer, SRV/UAV
//   바인딩 목록, 즉시 컨텍스트에 거는 Apply, 스테이지별 Set*ForStage,
//   그리고 SRV/UAV 해저드 해소.
//
//   전부 호출자가 0이었다. Material::ApplyShaderParams가 마지막 소비자였고
//   그 함수의 호출자도 0이다 — 저작 값은 여기까지 흘러오는데 GPU에 올리는
//   쪽이 없는 상태였다. 지형(T5)에서 본 것과 같은 구조다.
//
// 남은 것은 백엔드와 무관하다:
//   · 리플렉션이 뽑은 상수 버퍼 레이아웃(CBEntry·VariableDesc)
//   · 그 레이아웃에 맞춰 저작 값을 담는 CPU 버퍼(cpuData)
//   · 정점 입력 레이아웃 기술
//
// M3(DX12 커스텀 재질 경로)이 이것을 읽어 RHIFrame::UploadConstants로 올린다.
//-----------------------------------------------------------------------------

enum class ShaderStage
{
    Vertex,
    Pixel,
    Geometry,
    Hull,
    Domain
};

class ShaderPSO : public PipelineStateObject
{
public:
    struct CBBinding
    {
        ShaderStage stage;
        UINT        slot;
    };

    struct VariableDesc
    {
        std::string              name;
        UINT                     offset{};
        UINT                     size{};
        D3D_SHADER_VARIABLE_TYPE type{ D3D_SVT_VOID };
        D3D_SHADER_VARIABLE_CLASS varClass{ D3D_SVC_SCALAR };
    };

    struct CBEntry
    {
        std::string name;
        UINT        size = 0;
        std::vector<CBBinding>  binds;
        std::vector<VariableDesc> variables;

        /// 저작 값의 진실. 예전에는 이것을 만들자마자 ID3D11Buffer로 올렸는데,
        /// 그 버퍼를 GPU에 거는 코드가 사라진 뒤로는 올릴 곳이 없었다.
        std::vector<std::byte> cpuData;
    };

public:
    ShaderPSO();
    ~ShaderPSO() = default;
    ShaderPSO(const ShaderPSO& other);

    /// 붙어 있는 셰이더 전부를 리플렉션해 상수 버퍼 레이아웃을 세운다.
    void ReflectConstantBuffers();

    /// 정점 셰이더의 입력 시그니처에서 레이아웃 기술을 뽑는다.
    void CreateInputLayoutFromShader();

    /// 상수 버퍼 하나를 통째로 갱신한다. 크기가 다르면 거절한다.
    bool UpdateConstantBuffer(std::string_view name, const void* data, size_t size);

    template <typename T>
    bool UpdateConstantBuffer(std::string_view name, const T& data)
    {
        return UpdateConstantBuffer(name, &data, sizeof(T));
    }

    /// 상수 버퍼 안의 변수 하나를 갱신한다.
    bool UpdateVariable(std::string_view cbName, std::string_view varName, const void* data, size_t size);

    template <typename T>
    bool UpdateVariable(std::string_view cbName, std::string_view varName, const T& data)
    {
        return UpdateVariable(cbName, varName, &data, sizeof(T));
    }

    const FileGuid& GetShaderPSOGuid() const { return m_shaderPSOGuid; }
    void SetShaderPSOGuid(const FileGuid& guid) { m_shaderPSOGuid = guid; }

    const std::unordered_map<std::string, CBEntry>& GetConstantBuffers() const { return m_cbByName; }
	std::string m_shaderPSOName{ "UnnamedShaderPSO" };

	bool IsInvalidated() const { return m_isInvalidated; }
	void SetInvalidated(bool val) { m_isInvalidated = val; }

private:
    void ReflectShader(ID3D11ShaderReflection* reflection, ShaderStage stage);
    void AddOrMergeCB(ID3D11ShaderReflectionConstantBuffer* cb, const D3D11_SHADER_BUFFER_DESC& cbDesc, ShaderStage stage, UINT bindPoint);

private:
    std::unordered_map<std::string, CBEntry> m_cbByName;

	FileGuid m_shaderPSOGuid{}; //<-- File GUID of the shader PSO
	bool     m_isInvalidated{ false }; //<-- Set to true when shaders are reloaded
};
#else
class ShaderPSO : public PipelineStateObject
{
public:
    ShaderPSO() = default;
    ~ShaderPSO() = default;

    struct VariableDesc {};
    struct CBEntry {};
};
#endif // !DYNAMICCPP_EXPORTS
