#pragma once
#include "../../../RHI/RHIFormat.h"
#include <cstdint>

#include "../../Graph/EnhancedRenderPass.h"

class Texture;

// 볼류메트릭 포그 패스 (PHASE 3-6, 미구현 패스 이식 4차 · 마지막).
//
// ── 기존 DX11 VolumetricFogPass가 하는 일 ──
//
//   화면을 160x90x128의 프록셀(froxel) 격자로 덮는다. z는 지수 분포라
//   가까운 곳이 촘촘하다. 세 단계다:
//
//   ① 산란(컴퓨트) — 프록셀마다 월드 좌표를 구해 광원마다 위상 함수를
//      먹이고, 그림자맵과 구름 그림자로 가려짐을 곱한다. 결과를 지난
//      프레임 격자와 섞어(0.95) 시간축으로 재운다.
//   ② 누적(컴퓨트) — z를 따라 앞에서 뒤로 훑으며 투과율을 곱해 나간다.
//      프록셀마다 '여기까지 오는 동안 쌓인 산란광과 남은 투과율'이 된다.
//   ③ 합성(픽셀) — 화면 픽셀의 월드 좌표로 격자를 샘플해 씬 색에 얹는다.
//
//   ★ 3D 텍스처 셋을 든다: 산란용 핑퐁 둘과 누적 결과 하나. 핑퐁이
//     프레임을 넘겨 살아야 ①의 시간축 누적이 성립한다.
//
//   ★ 합성이 제자리에 쓰느라 씬 컬러를 통째로 복사한다(화면 크기 1회).
//
// ── 다시 쓰면서 바꾸는 것 ──
//
//   ★ 복사가 사라진다. 합성이 새 transient에 쓴다 — SSR·SSS와 같은 처리다.
//     이것으로 미구현 패스 넷에서 걷어낸 화면 크기 복사가 도합 여덟이 된다.
//
//   ★ 3D 텍스처는 그래프가 만들지 않고 패스가 든다. 프레임을 넘겨 살아야
//     하므로 transient가 될 수 없다 — 그래프는 프레임마다 새로 세워진다.
//     대신 매 프레임 ImportTexture로 들여보내 배리어를 그래프가 유도하게
//     한다. Import는 차원을 가리지 않아 그래프를 고칠 필요가 없었다.
//
//   ★ 첫 프레임에 격자를 0으로 지운다. DX11은 CreateTexture3D에 초기값을
//     주지 않아 내용이 미정의인데, 시간축 혼합이 지난 프레임을 95%로
//     받으므로 그 미정의가 첫 몇 프레임의 그림을 정한다. 드라이버가
//     0을 주는 것이 보통이라 결과는 같고, '보통'에 기대지 않게 된다.
//
//   블렌드·깊이를 명시한다(SSS·Decal·SSR과 같은 부류).
//
// ── 원본에서 발견한 것 넷 ──
//
//   전부 그대로 보존한다 — 기준선이 DX11이다.
//
//   ④ ThicknessFactor가 죽은 튜닝이다. 상수로 넘기고 UI 슬라이더까지
//      있지만, 그것을 쓰던 슬라이스 두께 계산이 누적 셰이더에서 주석
//      처리돼 있다(Frostbite식 적분으로 갈아탄 흔적이다). 슬라이더를
//      움직여도 그림이 바뀌지 않는다.
//
//   ① 태양 항이 주석 처리돼 있다. SunDirection·SunColor를 상수로 넘기고
//      Henyey-Greenstein까지 준비해 두었지만 그 두 줄이 주석이라, 실제
//      산란은 광원 목록(최대 20개)만으로 계산된다. 상수 둘은 죽어 있다.
//
//   ② 구름 그림자가 켬/끔을 보지 않는다. GetCloudVisibility가 isOn을
//      읽지 않고 곱해 버리므로, 구름 그림자를 꺼도 그 알파가 포그의
//      밀도를 그대로 좌우한다. 구름 맵이 안 물리면 가려짐이 0이 되어
//      포그가 통째로 사라진다.
//
//   ③ 누적 셰이더에 경계 검사가 없다. 산란은 texCoord.y < VolumeSize.y로
//      막지만 누적은 막지 않아, y가 90~95인 스레드가 격자 밖에 쓴다
//      (디스패치가 ceil(90/8)=12그룹 x 8 = 96행이다). D3D가 범위 밖
//      UAV 쓰기를 버려 주어 사고는 나지 않고, 낭비만 남는다.
class EnhancedVolumetricFogPass : public EnhancedRenderPass
{
public:
    static constexpr RHIFormat kVoxelFormat = RHIFormat::RGBA16Float;
    static constexpr RHIFormat kOutputFormat = RHIFormat::RGBA16Float;

    // DX11 그대로. 160x90은 Initialize가 박아 두고 z는 매크로다.
    static constexpr uint32_t kVolumeWidth = 160;
    static constexpr uint32_t kVolumeHeight = 90;
    static constexpr uint32_t kVolumeDepth = 128;

    /// 셰이더의 MAX_LIGHTS와 같아야 한다.
    static constexpr uint32_t kMaxLights = 20;

    /// DX11 VolumetricFogPassSetting 기본값 그대로.
    struct Tuning
    {
        float anisotropy{ 0.109f };
        float density{ 0.101f };
        float strength{ 2.0f };
        float thicknessFactor{ 0.01f };
        float blendingWithSceneColorFactor{ 0.851f };
        float previousFrameBlendFactor{ 0.95f };
        float customNearPlane{ 0.5f };
        float customFarPlane{ 1000.0f };
    };

    /// 구름 그림자 상수(셰이더 b1). 구름 그림자는 별개 계통이라 값은
    /// 호출부가 준다.
    ///
    /// ★ 중립값이 '전부 0'이 아니다. 위 ②대로 가려짐에 알파가 곱해지므로
    /// alpha가 0이면 포그가 통째로 사라진다. 구름을 쓰지 않을 때의 중립은
    /// '흰 맵 + alpha 1'이다.
    struct CloudShadow
    {
        Mathf::Matrix viewProjection{};
        float    cloudMapSize[2]{ 1.f, 1.f };
        float    size[2]{ 1.f, 1.f };
        float    direction[2]{ 0.f, 0.f };
        uint32_t frameIndex{ 0 };
        float    moveSpeed{ 0.f };
        float    alpha{ 1.f };
        int32_t  isOn{ 0 };   // 셰이더가 읽지 않는다(②)
    };

    const char* GetName() const override { return "VolumetricFog"; }

    bool Initialize(const EnhancedFrameContext& context, std::string& outError) override;
    bool PrepareFrame(const EnhancedFrameContext& context, std::string& outError) override;
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) override;
    void Shutdown() override;

    struct Inputs
    {
        /// 포그를 얹을 그림(라이팅 결과).
        RGHandle color;

        /// 씬 깊이. 화면 픽셀의 월드 좌표를 복원하는 데 쓴다.
        RGHandle depth;

        /// 캐스케이드 그림자맵(Texture2DArray). 셰이더가 슬라이스 2를
        /// 짚는다 — DX11이 마지막 캐스케이드 행렬을 넘기는 것과 짝이다.
        RGHandle shadowMap;

        /// 구름 그림자 맵. 위 ②대로 없으면 포그가 사라지므로 중립을 물려야 한다.
        RGHandle cloudShadow;

        /// 블루 노이즈. 프록셀 지터에 쓴다.
        RGHandle blueNoise;
    };

    void SetInputs(const Inputs& inputs) { m_inputs = inputs; }
    void SetTuning(const Tuning& tuning) { m_tuning = tuning; }
    // 설정 창이 '적용 후의 실제 값'을 되읽는다 — SSAO/SSGI/PostChain과 같은 규약.
    const Tuning& GetTuning() const { return m_tuning; }
    void SetCloudShadow(const CloudShadow& cloud) { m_cloud = cloud; }
    void SetEnabled(bool enabled) { m_enabled = enabled; }

    /// 그림자를 드리우는 방향광의 광원 행렬(마지막 캐스케이드).
    void SetShadowMatrix(const Mathf::Matrix& matrix) { m_shadowMatrix = matrix; }

    /// 프레임 번호. 지터의 씨앗이다(DX11은 Time->GetFrameCount()).
    void SetFrameIndex(uint32_t frameIndex) { m_frameIndex = frameIndex; }

    void SetKeepAlive(bool keepAlive) { m_keepAlive = keepAlive; }

    /// 포그가 얹힌 새 그림. 꺼져 있으면 입력 그대로.
    RGHandle GetOutput() const { return m_output; }

    /// 검증용. 누적까지 끝난 격자를 그래프 핸들로 내놓는다.
    RGHandle GetVoxelGrid() const { return m_finalHandle; }

    /// 이번 프레임에 셰이더로 넘긴 광원 수. 0이면 산란이 아무 빛도 못 받는다.
    uint32_t GetLastLightCount() const { return m_lastLightCount; }

private:
    bool CreatePipelines(const EnhancedFrameContext& context, std::string& outError);
    bool CreateVolumes(const EnhancedFrameContext& context, std::string& outError);

    Inputs      m_inputs{};
    Tuning      m_tuning{};
    CloudShadow m_cloud{};
    RGHandle    m_output;
    bool        m_enabled{ true };   // DX11 기본값은 켬이다(SSR과 반대)
    bool        m_keepAlive{ false };

    Mathf::Matrix m_shadowMatrix{};
    uint32_t      m_frameIndex{ 0 };

    // 프레임 밀봉 값(3-2).
    Mathf::Matrix  m_inverseViewProjection{};
    Mathf::Matrix  m_viewProjection{};
    Mathf::Matrix  m_inverseView{};
    Mathf::Matrix  m_inverseProjection{};
    Mathf::Vector4 m_cameraPosition{};

    /// 지난 프레임의 뷰프로젝션. 시간축 재투영에 쓴다.
    ///
    /// 둘로 나눈 이유: PrepareFrame이 이번 프레임 값을 저장해 두어야
    /// 다음 프레임이 쓰는데, Record는 실행 시점에 읽으므로 하나로 두면
    /// 이번 프레임 값을 '지난 프레임'이라며 읽게 된다. 밀봉본을 따로 둔다.
    Mathf::Matrix m_previousViewProjection{};
    Mathf::Matrix m_previousViewProjectionSealed{};

    // ── 격자 셋 ──
    //
    // 산란용 핑퐁 둘과 누적 결과 하나. 프레임을 넘겨 살아야 하므로
    // 그래프 transient가 아니라 패스가 든다.
    RHITextureHandle m_voxelTemp[2];
    RHITextureHandle m_voxelFinal;

    // 매 프레임 Import할 때 알려 줄 현재 상태.
    RHIResourceState m_voxelTempState[2]{ RHIResourceState::Common, RHIResourceState::Common };
    RHIResourceState m_voxelFinalState{ RHIResourceState::Common };

    /// 어느 쪽을 읽는가. DX11의 mCurrentTexture3DRead와 같은 역할이다.
    uint32_t m_readIndex{ 0 };
    bool     m_volumesCleared{ false };

    RGHandle m_finalHandle;

    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };
    uint32_t m_lastLightCount{ 0 };

    RHIPipelineHandle m_scatterPSO;
    RHIPipelineHandle m_accumulatePSO;
    RHIPipelineHandle m_compositePSO;
};

