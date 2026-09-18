#pragma once
// PHASE 21 W4 — 뷰포트 캔버스 규약 하나 (계획서 §1.5).
//
// 여기가 생기기 전에는 규약이 **둘**이었다. Scene 은 창 전체를 캔버스로 쓰면서
// `ImGuizmo::SetRect` 에 제목표시줄 높이를 손으로 더했고, Game 은 content region
// 에 종횡비를 지켜 letterbox 했다. 텍스처 ID 만 바꾸는 Host 는 그 차이를 흡수하지
// 못한다 — 기즈모와 picking 이 서로 다른 출처를 읽기 때문이다.
//
// 그래서 규약을 먼저 하나로 정한다.
//
//   ① 원점은 언제나 **창의 content region** 이다. 창 프레임이 아니다. 제목표시줄
//      높이를 되더하는 호출자는 없어야 한다.
//   ② 사각형은 전부 같은 프레임의 **화면 좌표** min/max 쌍이다. 크기는 언제나
//      `max - min` 으로 구한다 — 크기를 따로 들고 다니면 둘이 어긋난다.
//   ③ "늘림이냐 letterbox 냐" 는 캔버스가 아니라 **모드가 고르는 정책**이다.
//      Game 은 letterbox(게임 프레임버퍼의 종횡비가 곧 출력 계약이라 지켜야 한다).
//      둘을 한 정책으로 억지로 합치면 한쪽이 반드시 틀린다.
//
//      Scene 은 처음에 crop 이었다 — 렌더 타깃이 **창** 크기라 캔버스보다 크고,
//      확대 없이 잘라 보여주는 것이 맞았기 때문이다. extent 기반 resize 뒤에는
//      타깃이 이 캔버스를 위해 만들어지므로(캔버스 × 렌더 배율) `fill` 이다.
//      배율을 내리면 소스가 캔버스보다 작아지는데, 그때 crop 은 그림을 작게
//      그리고 가장자리를 검게 남긴다.
#include "imgui.h"
#include <algorithm>
#include <cmath>

namespace editor
{
    enum class viewport_fit
    {
        crop,       // 소스 픽셀 하나가 프레임버퍼 픽셀 하나. 확대하지 않는다.
        letterbox,  // 종횡비를 지켜 맞춘다. 남는 자리는 빈다.
        fill,       // 소스를 content 전체에 편다. 아래 설명 참고.
    };

    // 사각형 셋이 전부 화면 좌표다.
    //
    //   content — 모드가 받은 자리. 원점이 여기다.
    //   image   — 소스 전체가 놓일 자리. crop 에서는 content 를 넘을 수 있고
    //             letterbox 에서는 결코 넘지 않는다.
    //   clip    — 실제로 화면에 있는 부분(image ∩ content). 그리는 것도, 포인터가
    //             안에 있는지 묻는 것도 이 사각형이다.
    //
    // UV 는 image 를 기준으로 낸다 — 소스 전체가 image 에 대응하므로 clip 의 UV 는
    // 그 안의 부분 구간이 된다.
    struct ViewportCanvas
    {
        ImVec2 contentMin{}, contentMax{};
        ImVec2 imageMin{}, imageMax{};
        ImVec2 clipMin{}, clipMax{};
        ImVec2 uvMin{}, uvMax{};
        float sourceAspect{ 1.f };
        viewport_fit fit{ viewport_fit::crop };
        bool valid{};

        static ImVec2 Extent(ImVec2 min, ImVec2 max) noexcept
        { return { max.x - min.x, max.y - min.y }; }

        ImVec2 ContentExtent() const noexcept { return Extent(contentMin, contentMax); }
        ImVec2 ImageExtent() const noexcept { return Extent(imageMin, imageMax); }
        ImVec2 ClipExtent() const noexcept { return Extent(clipMin, clipMax); }

        ImVec2 SourceUV(ImVec2 screen) const noexcept
        {
            const ImVec2 extent = ImageExtent();
            return { (screen.x - imageMin.x) / extent.x, (screen.y - imageMin.y) / extent.y };
        }

        bool ClipContains(ImVec2 point) const noexcept
        {
            return valid && point.x >= clipMin.x && point.x < clipMax.x &&
                   point.y >= clipMin.y && point.y < clipMax.y;
        }
    };

    inline ViewportCanvas LayoutViewportCanvas(viewport_fit fit, ImVec2 contentMin,
        ImVec2 contentSize, ImVec2 sourcePixels, ImVec2 framebufferScale)
    {
        ViewportCanvas canvas{};
        canvas.fit = fit;
        const auto positive = [](float value) { return std::isfinite(value) && value > 0.f; };

        // ★ content 는 `valid` 와 **무관하게** 채운다.
        //
        //   content 는 모드가 받은 자리라 소스가 아직 없어도 알 수 있는 값이다.
        //   예전에는 여기서 통째로 조기 반환해 content 까지 0 으로 돌려줬고, 그
        //   탓에 소비자가 제 좌표를 따로 만들어 들고 다녔다 — 씬뷰가 `imageMin`
        //   이라는 이름의 지역 content 사각형을 만들어 오버레이에 넘기던 것이
        //   그것이다. 정본이 답할 수 있는 것을 답하지 않으면 정본이 둘이 된다.
        //
        //   `valid` 의 뜻은 바뀌지 않는다 — "그릴 소스가 있고 image/clip/uv 가
        //   섰는가" 이고, 그 셋은 아래에서만 채워진다.
        if (positive(contentSize.x) && positive(contentSize.y))
        {
            canvas.contentMin = contentMin;
            canvas.contentMax = { contentMin.x + contentSize.x, contentMin.y + contentSize.y };
        }

        if (!positive(contentSize.x) || !positive(contentSize.y) ||
            !positive(sourcePixels.x) || !positive(sourcePixels.y) ||
            !positive(framebufferScale.x) || !positive(framebufferScale.y)) return canvas;

        canvas.sourceAspect = sourcePixels.x / sourcePixels.y;

        ImVec2 imageSize{};
        if (viewport_fit::fill == fit)
        {
            // 소스가 **이 캔버스를 위해** 만들어진 경우다(extent 기반 resize).
            // 렌더 타깃 크기 = 캔버스 × 렌더 배율이므로 종횡비는 이미 같고,
            // 배율이 1 보다 작으면 소스가 캔버스보다 작다 — 그래도 자리는 캔버스
            // 전체다. Godot 의 `stretch_shrink` 가 적은 그대로 "divides the
            // effective resolution while preserving its scale" 이다. crop 으로
            // 두면 배율을 내린 만큼 그림이 작아지고 가장자리가 검게 남는다.
            imageSize = contentSize;
        }
        else if (viewport_fit::crop == fit)
        {
            // 논리 픽셀로 환산한 소스 크기 그대로. 패널이 크면 가장자리가 드러나고
            // 작으면 광학 중심을 기준으로 잘린다 — 확대는 하지 않는다.
            imageSize = { sourcePixels.x / framebufferScale.x, sourcePixels.y / framebufferScale.y };
        }
        else
        {
            imageSize = { contentSize.x, contentSize.x / canvas.sourceAspect };
            if (imageSize.y > contentSize.y)
            { imageSize = { contentSize.y * canvas.sourceAspect, contentSize.y }; }
        }

        canvas.imageMin = { contentMin.x + (contentSize.x - imageSize.x) * .5f,
                            contentMin.y + (contentSize.y - imageSize.y) * .5f };
        canvas.imageMax = { canvas.imageMin.x + imageSize.x, canvas.imageMin.y + imageSize.y };

        canvas.clipMin = { (std::max)(canvas.contentMin.x, canvas.imageMin.x),
                           (std::max)(canvas.contentMin.y, canvas.imageMin.y) };
        canvas.clipMax = { (std::min)(canvas.contentMax.x, canvas.imageMax.x),
                           (std::min)(canvas.contentMax.y, canvas.imageMax.y) };
        if (canvas.clipMax.x <= canvas.clipMin.x || canvas.clipMax.y <= canvas.clipMin.y) return canvas;

        canvas.uvMin = canvas.SourceUV(canvas.clipMin);
        canvas.uvMax = canvas.SourceUV(canvas.clipMax);
        canvas.valid = true;
        return canvas;
    }
}
