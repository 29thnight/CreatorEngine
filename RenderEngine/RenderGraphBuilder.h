#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include "DLLAcrossSingleton.h"

// 전방 선언: 엔진 쪽 Texture 래퍼
class Texture;

// =======================
// 기본 핸들 / enum 정의
// =======================

struct RGTextureHandle
{
    uint16_t index = 0xFFFF;

    static constexpr uint16_t Invalid = 0xFFFF;

    bool IsValid() const { return index != Invalid; }
};

struct RGPassHandle
{
    uint16_t index = 0xFFFF;

    static constexpr uint16_t Invalid = 0xFFFF;

    bool IsValid() const { return index != Invalid; }
};

enum class RGTextureAccess
{
    Read,
    Write,
    ReadWrite
};

// =======================
// 텍스처 / 패스 설명 구조체
// =======================

struct RGTextureDesc
{
    uint32_t    width = 0;
    uint32_t    height = 0;
    // DXGI_FORMAT 또는 엔진 자체 포맷 enum 등으로 나중에 확장 가능
    uint32_t    format = 0; // placeholder: DXGI_FORMAT을 그대로 uint32_t로 보관해도 됨

    // 필요 시 bindFlags, clearColor 등 추가
};

struct RGPassDesc
{
    std::string name;
};

// =======================
// RenderGraphBuilder
// =======================

class RenderGraphBuilder : public DLLCore::Singleton<RenderGraphBuilder>
{
public:
    using ExecuteCallback = std::function<void()>;
    friend class DLLCore::Singleton<RenderGraphBuilder>;

    // 내부 PassBuilder: AddPass()에서 setup 람다에 넘겨주는 빌더
    class PassBuilder
    {
    public:
        void ReadTexture(RGTextureHandle handle);
        void WriteTexture(RGTextureHandle handle, RGTextureAccess access = RGTextureAccess::Write);

        // 패스가 실제로 실행할 로직(람다)을 등록
        void SetExecuteCallback(const ExecuteCallback& callback);

    private:

        friend class RenderGraphBuilder;

        PassBuilder(RenderGraphBuilder& owner, std::size_t passIndex);

        RenderGraphBuilder& m_owner;
        std::size_t         m_passIndex;
    };

private:
    RenderGraphBuilder();
    ~RenderGraphBuilder();

public:
    // 그래프 초기화 (리소스/패스 모두 제거)
    void Reset();

    // 새 텍스처를 그래프 리소스로 등록 (Step1에서는 실제 생성은 안 하고 desc만 저장)
    RGTextureHandle AddTexture(const RGTextureDesc& desc);

    // 이미 엔진에서 만든 Texture*를 그래프에 import
    RGTextureHandle ImportExternalTexture(Texture* texture, const RGTextureDesc& desc);

    // 패스 등록
    // setup 람다 안에서 PassBuilder를 통해 Read/Write/Execute를 선언
    RGPassHandle AddPass(const RGPassDesc& desc,
        const std::function<void(PassBuilder&)>& setup);

    // [Step2] 그래프 분석 및 실행 순서 계산
    void Compile();

    // [Step2] Compile() 결과를 기반으로 올바른 순서로 패스를 실행
    void Execute();

private:
    struct TextureResource
    {
        RGTextureDesc desc;
        Texture* externalTexture = nullptr; // ImportExternalTexture로 들어온 경우만 사용
        bool    imported = false;          // true면 외부 텍스처, false면 그래프가 소유(예정)
    };

    struct TextureReadBinding
    {
        RGTextureHandle handle;
    };

    struct TextureWriteBinding
    {
        RGTextureHandle handle;
        RGTextureAccess access = RGTextureAccess::Write;
    };

    struct PassNode
    {
        RGPassDesc                      desc;
        std::vector<TextureReadBinding>  reads;
        std::vector<TextureWriteBinding> writes;
        ExecuteCallback                 execute;
    };

private:
    std::vector<TextureResource> m_textures;
    std::vector<PassNode>        m_passes;

    // [Step2] Compile()에서 계산된 실행 순서 (패스 인덱스들)
    std::vector<std::size_t>     m_executeOrder;
};
