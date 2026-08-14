#pragma once
#include "../../../RHI/RHIFormat.h"
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <vector>
#include <wrl/client.h>

#include "../../Graph/EnhancedRenderPass.h"

// UI·스프라이트 패스 (PHASE 3-6, 신규 작성).
//
// ── 기존 DX11 UIPass가 하는 일과 그 값 ──
//
//   for (auto* proxy : renderData->m_UIRenderQueue)
//   {
//       spriteBatch->Begin(SpriteSortMode_FrontToBack, ...);
//       proxy->Draw(spriteBatch);
//       spriteBatch->End();
//   }
//
// ★ 프록시마다 Begin/End를 한다. SpriteBatch가 존재하는 이유가 여러
//   스프라이트를 한 드로우로 묶는 것인데, 하나 넣고 닫으면 그 이유가
//   통째로 사라진다 — UI 요소가 100개면 드로우콜도 100개다.
//   SortMode를 FrontToBack으로 주지만 배치 안에 하나뿐이라 정렬할 것도 없다.
//
//   게다가 프레임마다 SpriteBatch를 새로 만든다(힙 할당 + DX11 상태 객체).
//
// ── 다시 쓰면서 바꾸는 것 ──
//
//   1. 사각형을 인스턴스로 모은다. 정점 버퍼를 두지 않고 SV_VertexID로
//      네 정점을 만들고, 사각형마다 위치·크기·UV·색을 인스턴스 자료에서
//      뽑는다. 업로드가 사각형당 float 열여섯 개면 끝난다.
//
//   2. 같은 텍스처를 쓰는 연속 사각형을 한 드로우로 묶는다.
//
//      ★ 여기서 GBuffer 인스턴싱과 갈린다. GBuffer는 (메시, 재질)로 전역
//        정렬해서 묶었다 — 불투명이라 그리는 순서가 그림을 바꾸지 않기
//        때문이다. UI는 그 반대다. 순서가 곧 위아래이므로 전역 정렬을
//        하면 겹친 요소의 앞뒤가 뒤집힌다.
//
//        그래서 순서는 (canvasOrder, layerOrder)로 안정 정렬해 그대로 두고,
//        '연속해서 같은 텍스처'인 구간만 묶는다. 실제 UI는 아틀라스 하나를
//        쓰는 요소가 줄지어 오는 것이 보통이라 이것으로 대부분 묶인다.
//
//   3. 배치 수를 통계로 센다. 배칭이 도는지는 그림으로 알 수 없고,
//      묶이지 않아도 화면은 똑같이 나온다 — GBuffer에서 '드로우 704에
//      배치 704'를 수치로 보고서야 병합이 한 건도 안 일어난 것을 알았다.
//
// ── 실측 (dx12.ui, 256x256) ──
//
//   같은 텍스처 사각형 넷        → 배치 1
//   텍스처를 번갈아 둔 사각형 넷 → 배치 4
//   겹친 곳 (R 0.000 B 1.000)    — layerOrder가 순서를 정한다
//   반투명  (R 0.500 G 0.500)    — 알파 0.5가 정확히 반반으로 섞인다
//
//   ★ 배치 수를 두 배치로 나란히 재는 것이 요점이다. '배치 1'만 보면
//     '묶었다'와 '애초에 하나였다'가 구분되지 않는다.
//
// 텍스트(SpriteFont)는 이 슬라이스에 넣지 않는다. 폰트 아틀라스 생성과
// 글리프 배치가 따로 필요하고, 그것까지 한 번에 하면 무엇이 실패했는지
// 분리되지 않는다. 사각형이 서고 나서 그 위에 얹는다.
class EnhancedUIPass : public EnhancedRenderPass
{
public:
    static constexpr RHIFormat kOutputFormat = RHIFormat::RGBA16Float;

    /// 사각형 하나. 화면 좌표(픽셀)로 받는다 — UI는 화면에 붙는 것이라
    /// 월드 변환을 거칠 이유가 없고, 거치면 해상도가 바뀔 때마다 어긋난다.
    struct Rect
    {
        float left{ 0.f };
        float top{ 0.f };
        float right{ 0.f };
        float bottom{ 0.f };

        /// 텍스처 안에서 잘라 쓸 구간(0~1). 아틀라스를 쓰면 여기가 갈린다.
        float uvLeft{ 0.f };
        float uvTop{ 0.f };
        float uvRight{ 1.f };
        float uvBottom{ 1.f };

        Mathf::Color4 color{ 1.f, 1.f, 1.f, 1.f };

        /// 그리는 순서. 같은 값이면 목록 순서를 지킨다(안정 정렬).
        int32_t canvasOrder{ 0 };
        int32_t layerOrder{ 0 };

        /// 없으면 1x1 흰색이 묶인다 — 단색 사각형이 그것으로 나온다.
        Texture* texture{ nullptr };
    };

    const char* GetName() const override { return "UI"; }

    bool Initialize(const EnhancedFrameContext& context, std::string& outError) override;
    bool PrepareFrame(const EnhancedFrameContext& context, std::string& outError) override;
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) override;
    void Shutdown() override;

    struct Inputs
    {
        /// UI가 그 위에 얹힐 그림. 비어 있으면 투명에서 시작한다.
        RGHandle color;
    };

    void SetInputs(const Inputs& inputs) { m_inputs = inputs; }
    void SetRects(const std::vector<Rect>* rects) { m_rects = rects; }

    /// UI 큐를 사각형 목록으로 옮긴다.
    ///
    /// 프록시를 그대로 들지 않고 필요한 것만 복사한다 — 렌더가 게임
    /// 자료구조를 들고 다니면 수명과 스레드 규약이 다시 얽힌다(3-2에서
    /// 걷어낸 부류다).
    ///
    /// 클리핑은 DX11 경로와 같은 함수(UIClipping.h)를 쓴다. 두 곳에 따로
    /// 두면 하나가 틀려도 알 수 없다.
    ///
    /// 텍스트·스프라이트시트는 아직 건너뛴다. 건너뛴 수를 돌려주므로
    /// '아직 안 되는 것'과 '되는데 안 나오는 것'이 구분된다.
    /// 컨테이너 종류를 묻지 않으려고 포인터와 개수로 받는다 — 엔진의 UI
    /// 큐는 concurrent_vector라, 그 타입을 이 헤더가 알 이유가 없다.
    static uint32_t BuildRectsFromQueue(
        class UIRenderProxy* const* proxies, size_t count,
        std::vector<Rect>& outRects);

    RGHandle GetOutput() const { return m_output; }

    /// 배칭이 실제로 도는지 보는 수. 사각형 수와 배치 수가 같으면 한 건도
    /// 안 묶인 것이고, 그건 그림으로는 절대 드러나지 않는다.
    uint32_t GetLastRectCount() const { return m_lastRectCount; }
    uint32_t GetLastBatchCount() const { return m_lastBatchCount; }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool CreatePipelines(const EnhancedFrameContext& context, std::string& outError);

    /// 인스턴스 자료. 셰이더의 구조체와 정확히 같아야 한다.
    struct RectInstance
    {
        Mathf::Vector4 bounds{};   // left · top · right · bottom (픽셀)
        Mathf::Vector4 uv{};       // uvLeft · uvTop · uvRight · uvBottom
        Mathf::Color4  color{};
    };

    struct Batch
    {
        uint32_t first{ 0 };
        uint32_t count{ 0 };
        Texture* texture{ nullptr };
    };

    Inputs   m_inputs{};
    RGHandle m_output;

    const std::vector<Rect>* m_rects{ nullptr };

    std::vector<RectInstance> m_instances;
    std::vector<Batch>        m_batches;

    uint32_t m_lastRectCount{ 0 };
    uint32_t m_lastBatchCount{ 0 };

    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };

    RHIPipelineHandle m_pso;
};

#endif
