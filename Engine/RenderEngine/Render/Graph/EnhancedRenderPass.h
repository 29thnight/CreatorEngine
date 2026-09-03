#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include <mathematics/color.hpp>
#include <mathematics/matrix4x4.hpp>
#include <mathematics/transform.hpp>
#include <mathematics/vector2.hpp>
#include <mathematics/vector4.hpp>

#include "EnhancedRenderGraph.h"
#include "../../RHI/IRenderDeviceServices.h"
#include "../../RHI/IRenderPipelineCache.h"
#include "../../RHI/IRenderTextureCache.h"
#include "../../RHI/RHIShaderPermutation.h"
#include "../../FrameCameraSnapshot.h"
#include "../../ShaderMetaHandle.h"
#include "../../ShaderMetaReflection.h"

class Mesh;
class Texture;

// M6-P1b2a: Texture*의 배열 순서가 shader register를 암묵적으로 뜻하지 않게 한다.
// ShaderMeta reflection이 해석한 논리 property/GUID/register와 CPU generation owner를
// 한 레코드로 밀봉한다. 실제 backend binding은 register를 검증한 뒤 owner의 view를 쓴다.
struct EnhancedMaterialTextureBinding
{
    std::string propertyName{};
    FileGuid textureGuid{};
    std::uint32_t registerIndex{};
    std::uint32_t registerSpace{};
    std::shared_ptr<Texture> textureOwner{};
};

// M6-P2d-b: Material::m_flowInfo와 producer frame time을 같은 immutable
// draw 경계에 밀봉한다. ShaderMeta property b2는 authored 숫자 정본이므로
// dynamic 값을 그 byte block에 덮어쓰지 않는다. Forward instance upload가
// 이 32B를 그대로 소비해 Foliage를 포함한 각 draw의 flow를 보존한다.
struct EnhancedForwardMaterialFlowSnapshot
{
    math::vector4 windVector{ 0.f, 0.f, 0.f, 0.f };
    math::vector2 uvScroll{ 0.f, 0.f };
    float totalSeconds{ 0.f };
    float deltaSeconds{ 0.f };

    std::array<float, 8> Values() const noexcept
    {
        return { windVector.x, windVector.y, windVector.z, windVector.w,
            uvScroll.x, uvScroll.y, totalSeconds, deltaSeconds };
    }

    bool IsFinite() const noexcept
    {
        const auto values = Values();
        return std::all_of(values.begin(), values.end(),
            [](float value) { return std::isfinite(value); });
    }
};

static_assert(sizeof(EnhancedForwardMaterialFlowSnapshot) == 32u);
static_assert(offsetof(EnhancedForwardMaterialFlowSnapshot, windVector) == 0u);
static_assert(offsetof(EnhancedForwardMaterialFlowSnapshot, uvScroll) == 16u);
static_assert(offsetof(EnhancedForwardMaterialFlowSnapshot, totalSeconds) == 24u);
static_assert(offsetof(EnhancedForwardMaterialFlowSnapshot, deltaSeconds) == 28u);
static_assert(std::is_standard_layout_v<EnhancedForwardMaterialFlowSnapshot>);
static_assert(std::is_trivially_copyable_v<EnhancedForwardMaterialFlowSnapshot>);

// M6-P2a/P2b: 제품 Forward draw의 값/texture generation/ShaderMeta permutation을
// 한 immutable packet으로 닫는다. P2d-c부터 texture schema는 ShaderMeta에 선언된
// 임의 이름/개수를 owner vector로 보존하고, pass가 reflection register를 자기 물리
// 슬롯에 투영한다. 기존 scalar는 legacy ShadeInstance 호환용이며 제품 material 값의
// 정본은 propertyBytes(b2)다.
struct EnhancedForwardMaterialDrawSnapshot
{
    ShaderMetaHandle shaderMetaHandle{};
    RHIShaderPermutationKey permutationKey{};
    ShaderMetaBindingLayout bindingLayout{};
    std::vector<std::uint16_t> keywordSelections{};
    std::vector<std::uint8_t> propertyBytes{};
    EnhancedForwardMaterialFlowSnapshot flow{};
    math::color baseColorFactor{ 1.f, 1.f, 1.f, 1.f };
    float metallic{ 0.f };
    float roughness{ 1.f };
    std::uint32_t useNormalMap{ 0 };
    std::vector<EnhancedMaterialTextureBinding> textureBindings{};

    bool IsValid() const noexcept
    {
        return useNormalMap <= 1u
            && flow.IsFinite()
            && shaderMetaHandle.IsValid()
            && !bindingLayout.constantBufferName.empty()
            && 2u == bindingLayout.constantBufferRegister
            && 0u == bindingLayout.constantBufferSpace
            && bindingLayout.constantBufferByteSize == propertyBytes.size();
    }
};

// M6-P1a: Material의 게임 객체 주소를 RT draw packet까지 운반하지 않는다.
// BuildDrawPool이 안정된 프레임 경계에서 generation/layout/property 값을 복사해
// 이 소유 snapshot을 만들고, GBuffer는 이것만으로 batch key와 b2를 결정한다.
// M6-P1b1부터 texture 넷도 shared owner를 복사한다. M6-P1b2a부터는 그 owner가
// 어느 논리 property/GUID/register의 generation인지 함께 고정한다.
struct EnhancedMaterialDrawSnapshot
{
    ShaderMetaHandle shaderMetaHandle{};
    // M6-P1b2b1: 같은 ShaderMeta generation 안에서도 keyword 선택이 다르면
    // 다른 compile/PSO identity다. authored index 배열과 함께 정규화된 key를
    // 밀봉해 RT가 문자열/축 순서를 다시 추측하지 않게 한다.
    RHIShaderPermutationKey permutationKey{};
    ShaderMetaBindingLayout bindingLayout{};
    std::vector<std::uint16_t> keywordSelections{};
    std::vector<std::uint8_t> propertyBytes{};
    // normalMap 슬롯에는 항상 폴백 texture까지 묶이므로 texture handle만으로는
    // 저작된 normal map의 유무를 셰이더가 구분할 수 없다. sealing 때 계산한
    // 의미 상태를 snapshot이 함께 소유한다.
    std::uint32_t useNormalMap{ 0 };
    std::vector<EnhancedMaterialTextureBinding> textureBindings{};

    bool IsValid() const noexcept
    {
        return useNormalMap <= 1u
            && shaderMetaHandle.IsValid()
            && !bindingLayout.constantBufferName.empty()
            && bindingLayout.constantBufferByteSize == propertyBytes.size();
    }
};

// 백엔드 서비스는 인터페이스로 받는다(PHASE 3-1 재정의, R1).
//
// 예전에는 여기에 구현 클래스 다섯(DX12DeviceResources · DX12PSOManager ·
// DX12RootSignatureCache · DX12MeshCache · DX12TextureCache)이 전방 선언돼
// 있었고, 패스가 DX12에 닿는 모든 경로가 아래 EnhancedFrameContext를 지났다 —
// 그래서 여기가 첫 절단선이 됐다. 메서드 이름을 그대로 둔 덕에 패스 본문
// 149곳은 한 줄도 바뀌지 않는다(RenderFrameServices.h 주석 참조).
//
// ★ 전방 선언이 아니라 include다. 패스 .cpp가 인터페이스의 완전 정의를
//   보장받아야 하는데, 전방 선언으로 두면 그 .cpp가 구현 헤더를 어쩌다
//   include했을 때만 컴파일된다 — 유니티 빌드에서는 청크 구성에 따라
//   우연히 통과했다가 파일 하나를 더하는 순간 깨진다(이 코드베이스가
//   이미 두 번 겪은 함정이다). R6-c 이후 그래프 헤더도 backend-neutral
//   include만 가지므로 구현 헤더에 기대는 우연한 완전 정의는 없다.

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
    // I6-C — legacy 업로드 폴백의 **원본**으로만 남는다. 신원·정렬·지오메트리
    // 맵 키는 아래 geometryKey가 진다. 폴백(Assimp 모델·A/B off)이 죽는 I6-E에서
    // 이 필드도 사라진다.
    Mesh*          mesh{ nullptr };
    // I6-C — 지오메트리 신원. experiment 핸들이 있으면 그 stableKey(자산
    // 신원 ⊕ 메시 인덱스), 없으면 legacy Mesh의 m_hashingMesh다. 둘은 D4b가
    // 적은 대로 **같은 64비트 키 공간**을 공유하고 충돌 무시 가정도 같다.
    // 0이면 '그릴 지오메트리 없음'이다 — 패스의 null 가드가 이 값을 본다.
    //
    // 포인터로 정렬하면 실행마다 순서가 달라질 수 있고(주소는 할당 순서),
    // 무엇보다 **렌더가 게임 객체 주소를 신원으로 쓰는 것** 자체가 I6에서
    // 지우려는 결합이다. 키는 자산 신원에서 나온다.
    std::size_t    geometryKey{ 0 };
    // I6-C — 그림자 캐스터 반경. 예전에는 Shadow 패스가 draw.mesh를
    // 역참조해 읽었다(legacy Mesh의 **데이터** 소비 마지막 자리).
    float          boundRadius{ 0.f };
    // MBC6 제품 뷰. MBC7 scene cutover가 채우며, 패스/RHI는 이 값이 있으면
    // legacy Mesh보다 먼저 소비한다(MBC9: 모델은 이 뷰만 탄다).
    RHIModelMeshView modelMeshView{};

    math::matrix4x4 worldMatrix{};

    // 재질에서 뽑아 온 것. Material* 자체를 들지 않는 이유는 메시와 같다 —
    // 렌더가 게임 자료구조를 들고 다니면 수명과 스레드 규약이 다시 얽힌다.
    Texture*       baseColor{ nullptr };
    Texture*       normalMap{ nullptr };
    Texture*       occRoughMetal{ nullptr };
    Texture*       emissive{ nullptr };

    math::color    baseColorFactor{ 1.f, 1.f, 1.f, 1.f };
    float          metallic{ 0.f };
    float          roughness{ 1.f };
    uint32_t       useNormalMap{ 0 };

    // M6-P1a 제품 GBuffer 경로는 이 immutable 값만 본다. 위 legacy
    // factor/texture 필드는 아직 snapshot을 만들지 않는 격리 selftest의 호환
    // 경계다.
    std::shared_ptr<const EnhancedMaterialDrawSnapshot> materialSnapshot{};

    // M6-P2a 제품 Forward 경로의 immutable 값/texture generation owner.
    // Forward pass는 이 값이 있으면 위 legacy field를 읽지 않는다. P2b에서
    // ShaderMeta generation/permutation/property block을 같은 경계에 합친다.
    std::shared_ptr<const EnhancedForwardMaterialDrawSnapshot>
        forwardMaterialSnapshot{};

    // ── 스키닝 ──
    //
    // 본 팔레트는 렌더 전용 버퍼(RenderScene::AnimationPalette)를 가리킨다.
    // mesh·Texture와 같은 계약이다 — 포인터의 수명은 draws 목록을 만든
    // 호출부가 보장하고, 패스는 PrepareFrame에서 GPU로 복사한 뒤로는
    // 이 포인터를 보지 않는다.
    //
    // animatorKey는 중복 제거용이다. 한 애니메이터를 여러 프록시가 공유하면
    // (한 캐릭터의 메시 여럿) 팔레트를 한 번만 올린다.
    const math::matrix4x4* bonePalette{ nullptr };
    uint64_t              animatorKey{ 0 };
    uint32_t              boneCount{ 0 };
};

static_assert(std::is_same_v<decltype(EnhancedDrawItem::worldMatrix),
    math::matrix4x4>);
static_assert(std::is_same_v<decltype(EnhancedDrawItem::baseColorFactor),
    math::color>);
static_assert(std::is_same_v<decltype(EnhancedDrawItem::bonePalette),
    const math::matrix4x4*>);
static_assert(sizeof(math::matrix4x4) == sizeof(float) * 16u);
static_assert(std::is_trivially_copyable_v<math::matrix4x4>);

// 셰이더가 읽는 형태의 광원 하나.
//
// 엔진의 Light를 그대로 쓰지 않는 이유는 크기다 — Light는 감쇠 계수·그림자
// 행렬까지 들고 있어 255개면 상수 버퍼 한도를 넘본다. 라이팅에 실제로 필요한
// 것만 추린다. 배치는 HLSL 쪽과 맞춰야 하므로 16바이트 경계를 지킨다.
struct EnhancedLight
{
    math::vector4  position{};       // w = 타입 (0 방향광 · 1 점광 · 2 스포트)
    math::vector4  direction{};      // w = 스포트 각도(라디안)
    math::color    color{};          // rgb 색 · a 세기
    math::vector4  attenuation{};    // x 상수 · y 선형 · z 이차 · w 반경
};

static_assert(sizeof(EnhancedLight) == 64u);
static_assert(offsetof(EnhancedLight, position) == 0u);
static_assert(offsetof(EnhancedLight, direction) == 16u);
static_assert(offsetof(EnhancedLight, color) == 32u);
static_assert(offsetof(EnhancedLight, attenuation) == 48u);
static_assert(std::is_same_v<decltype(EnhancedLight::color), math::color>);
static_assert(std::is_standard_layout_v<EnhancedLight>);
static_assert(std::is_trivially_copyable_v<EnhancedLight>);

// 캐스케이드 그림자의 단수. 셋은 실측 관행에서 온 수다 — 하나면 가까운 곳의
// 해상도를 위해 먼 곳을 포기해야 하고, 넷 이상은 정점 처리와 맵 메모리가 늘어난
// 만큼의 화질을 돌려주지 않는다. 기존 DX11 경로(ShadowMapPass)도 셋이다.
inline constexpr uint32_t kShadowCascadeCount = 3;

// 그림자 패스가 만들고 라이팅 패스가 읽는 것. 두 패스가 이 구조를 공유하는 편이
// 인자를 낱개로 넘기는 것보다 낫다 — 캐스케이드가 늘거나 줄 때 서명이 아니라
// 이 구조만 바뀐다.
struct EnhancedShadowData
{
    math::matrix4x4 lightViewProjection[kShadowCascadeCount]{};

    // 각 캐스케이드가 끝나는 뷰 깊이(카메라 정면 방향 거리). 픽셀이 어느
    // 캐스케이드에 속하는지 셰이더가 이 값으로 고른다.
    math::vector4 splitDepths{};

    // xyz = 캐스케이드별 깊이 편향. 먼 캐스케이드는 텍셀 하나가 덮는 월드
    // 범위가 넓어 같은 편향으로는 여드름이 남는다 — 반지름 비로 키운다.
    // w = 경사 비례 계수. 표면이 빛과 비스듬할수록 텍셀 하나 안에서 깊이가
    // 크게 변해 상수 편향으로는 모자란다 — tan(경사각)에 비례해 키운다.
    math::vector4 bias{};

    // 캐스케이드 경계 블렌딩 폭(분할 깊이에 대한 비율). 경계에서 두 캐스케이드의
    // 해상도 차이가 그림자 가장자리를 어긋나게 만들고, 하드 스위치면 그 어긋남이
    // 선으로 보인다 — 경계 앞 구간에서 다음 캐스케이드와 섞는다. 0이면 끈다.
    float cascadeBlendBand{ 0.f };

    // 뷰 깊이를 구하려면 카메라 정면이 필요하다. 뷰 행렬에서 뽑을 수도 있지만
    // 셰이더가 매 픽셀 그것을 하는 것보다 넘기는 편이 싸다.
    math::vector4 cameraForward{};

    // 그림자를 드리우는 방향광의 방향(정규화). 라이팅은 광원 목록에서 같은
    // 값을 얻지만, 캐스터 컬링을 밖에서 검증하려면 이쪽이 필요하다.
    math::vector4 lightDirection{};

    bool enabled{ false };
};

static_assert(std::is_trivially_copyable_v<EnhancedShadowData>);

// 한 프레임의 렌더 입력과 도구. 패스는 여기 있는 것만 쓴다 —
// 전역 DeviceStates를 만지지 않는 것이 3-6의 규약이다.
struct EnhancedFrameContext
{
    IRenderDeviceServices*     resources{ nullptr };
    IRenderPipelineCache*      psoManager{ nullptr };
    IRenderRootSignatureCache* rootSignatures{ nullptr };
    IRenderMeshCache*          meshCache{ nullptr };
    IRenderTextureCache*       textureCache{ nullptr };

    uint32_t width{ 0 };
    uint32_t height{ 0 };

    // 프레임 밀봉된 카메라(3-2). 살아 있는 Camera를 읽지 않는다.
    const FrameCameraSnapshot* camera{ nullptr };

    // 이 프레임에 그릴 것들. 비어 있으면 패스는 클리어만 한다.
    //
    // draws는 불투명(deferred) 큐다 — GBuffer가 그린다.
    const std::vector<EnhancedDrawItem>* draws{ nullptr };

    // 포워드 큐. Forward+가 그린다.
    //
    // draws와 나누는 이유는 두 큐가 서로 다른 파이프라인을 타기 때문이다.
    // 하나로 합쳐 두면 GBuffer가 포워드 물체까지 GBuffer에 기록하거나
    // Forward+가 이미 deferred로 그려진 것을 한 번 더 그리게 되는데,
    // 둘 다 '그림이 조금 이상하다'로만 드러나 원인을 찾기 어렵다.
    const std::vector<EnhancedDrawItem>* forwardDraws{ nullptr };

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
