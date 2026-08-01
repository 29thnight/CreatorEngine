#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <string>
#include <vector>

#include "EnhancedRenderGraph.h"
#include "../../FrameCameraSnapshot.h"

class Mesh;
class Texture;
class DX12DeviceResources;
class DX12PSOManager;
class DX12RootSignatureCache;
class DX12MeshCache;
class DX12TextureCache;

// EnhancedRenderPass — DX12 렌더 패스의 기반 (PHASE 3-6).
//
// 기존 IRenderPass를 상속하지 않는다. 그쪽은 CreateRenderCommandList(context, scene, camera)
// 하나로 '이 패스가 지금 다 그린다'를 전제하는데, 그래프 위에서는 선언과 기록이 나뉜다:
//   Declare  — 무슨 리소스를 어떤 상태로 쓸지 그래프에 알린다(기록하지 않는다)
//   Record   — 그래프가 정한 시점에 커맨드를 기록한다(배리어는 그래프가 이미 넣었다)
//
// 이 분리가 필요한 이유는 둘이다. 배리어를 그래프가 유도하려면 기록 전에 사용 계획을
// 알아야 하고(3-5), 기록을 워커가 병렬로 돌리려면 기록이 순수해야 한다 —
// 기록 중에 리소스를 만들거나 상태를 바꾸면 병렬화가 성립하지 않는다.
//
// 그래서 규약을 셋 둔다:
//   ① Declare에서 리소스를 만들거나 커맨드를 기록하지 않는다
//   ② Record에서 리소스를 만들거나 배리어를 넣지 않는다
//   ③ Record는 넘겨받은 커맨드 리스트에만 기록한다(전역 상태를 만지지 않는다)

// 한 번의 드로우. 씬에서 뽑아 온 것이고, 패스는 이 목록만 순회한다.
//
// 프록시를 그대로 넘기지 않는 이유: 프록시는 게임 쪽 자료구조라 렌더가 그것을
// 직접 읽으면 3-2에서 걷어낸 부류(렌더가 게임 상태를 만짐)가 되살아난다.
// 프레임을 밀봉할 때 필요한 것만 복사해 온다.
struct EnhancedDrawItem
{
    Mesh*          mesh{ nullptr };
    Mathf::xMatrix worldMatrix{};

    // 재질에서 뽑아 온 것. Material* 자체를 들지 않는 이유는 메시와 같다 —
    // 렌더가 게임 자료구조를 들고 다니면 수명과 스레드 규약이 다시 얽힌다.
    Texture*       baseColor{ nullptr };
    Texture*       normalMap{ nullptr };
    Texture*       occRoughMetal{ nullptr };
    Texture*       emissive{ nullptr };

    Mathf::Color4  baseColorFactor{ 1.f, 1.f, 1.f, 1.f };
    float          metallic{ 0.f };
    float          roughness{ 1.f };
    uint32_t       useNormalMap{ 0 };
};

// 셰이더가 읽는 형태의 광원 하나.
//
// 엔진의 Light를 그대로 쓰지 않는 이유는 크기다 — Light는 감쇠 계수·그림자
// 행렬까지 들고 있어 255개면 상수 버퍼 한도를 넘본다. 라이팅에 실제로 필요한
// 것만 추린다. 배치는 HLSL 쪽과 맞춰야 하므로 16바이트 경계를 지킨다.
struct EnhancedLight
{
    Mathf::Vector4 position{};       // w = 타입 (0 방향광 · 1 점광 · 2 스포트)
    Mathf::Vector4 direction{};      // w = 스포트 각도(라디안)
    Mathf::Color4  color{};          // rgb 색 · a 세기
    Mathf::Vector4 attenuation{};    // x 상수 · y 선형 · z 이차 · w 반경
};

// 한 프레임의 렌더 입력과 도구. 패스는 여기 있는 것만 쓴다 —
// 전역 DeviceStates를 만지지 않는 것이 3-6의 규약이다.
struct EnhancedFrameContext
{
    DX12DeviceResources*    resources{ nullptr };
    DX12PSOManager*         psoManager{ nullptr };
    DX12RootSignatureCache* rootSignatures{ nullptr };
    DX12MeshCache*          meshCache{ nullptr };
    DX12TextureCache*       textureCache{ nullptr };

    uint32_t width{ 0 };
    uint32_t height{ 0 };

    // 프레임 밀봉된 카메라(3-2). 살아 있는 Camera를 읽지 않는다.
    const FrameCameraSnapshot* camera{ nullptr };

    // 이 프레임에 그릴 것들. 비어 있으면 패스는 클리어만 한다.
    const std::vector<EnhancedDrawItem>* draws{ nullptr };

    // 이 프레임의 광원. 씬의 Light를 그대로 들지 않고 셰이더가 쓰는 형태로
    // 복사해 온다 — 메시·재질과 같은 이유다.
    const std::vector<EnhancedLight>* lights{ nullptr };
};

class EnhancedRenderPass
{
public:
    virtual ~EnhancedRenderPass() = default;

    virtual const char* GetName() const = 0;

    /// 한 번만 부른다. PSO·루트 시그니처·정적 리소스를 준비한다.
    virtual bool Initialize(const EnhancedFrameContext& context, std::string& outError) = 0;

    /// 프레임마다, Declare보다 먼저 부른다. 이번 프레임에 필요한 GPU 리소스를
    /// 올린다(메시 업로드 등).
    ///
    /// 이 단계가 따로 있는 이유는 규약 때문이다. Declare는 선언만 하고 Record는
    /// 리소스를 만들지 않기로 했는데, 업로드는 둘 다에 맞지 않는다 — 커맨드
    /// 기록을 동반하면서 리소스도 만든다. 그래서 프레임이 열린 직후, 그래프가
    /// 돌기 전의 자리를 따로 준다.
    virtual bool PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
    {
        (void)context; (void)outError;
        return true;
    }

    /// 프레임마다 부른다. 그래프에 리소스 사용과 실행을 등록한다.
    /// 여기서 리소스를 만들거나 커맨드를 기록하면 안 된다.
    virtual void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) = 0;

    virtual void Shutdown() {}
};

#endif
