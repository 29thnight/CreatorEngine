#pragma once
#include "../../../Utility_Framework/Ownership.h"
// 보유층의 광원 프록시를 뷰가 쓸 형태로 고르고 옮겨 담는다.
//
// ★ 함수로 뽑은 이유. 이 변환이 라이브 러너와 자가 검증 두 곳에 손으로
//   복사돼 있었고, 두 곳이 갈리면 "검증은 통과하는데 실전만 다른 그림"이
//   된다 — 그 부류는 원인을 찾기가 특히 나쁘다.
//
// ★ 세기는 여기서 곱하지 않는다. color.rgb는 저작 색 그대로, color.a가
//   세기이고 곱은 셰이더의 rgb*a 한 번뿐이다. 예전에는 컴포넌트가 색에
//   미리 곱한 값을 싣고 여기서 세기를 a에 또 실어 제곱이 됐다.
#include "../Graph/EnhancedRenderPass.h"
#include "../../LightRenderProxy.h"
#include "../../FrameCameraSnapshot.h"

#include <mathematics/frustum.hpp>
#include "Mathematics.Intersect.h"
#include "../../../Utility_Framework/StandardLibrary.h"

inline EnhancedLight MakeEnhancedLight(const LightRenderProxy& source)
{
    EnhancedLight light{};

    light.position = math::vector4{
        source.m_worldPosition.x, source.m_worldPosition.y, source.m_worldPosition.z,
        static_cast<float>(source.m_lightType) };

    light.direction = math::vector4{ source.m_direction.x, source.m_direction.y,
        source.m_direction.z, math::radians(source.m_spotLightAngle) };

    light.color = source.m_color;
    light.color.a = source.m_intensity;

    light.attenuation = math::vector4{
        source.m_constantAttenuation, source.m_linearAttenuation,
        source.m_quadraticAttenuation, source.m_range };

    return light;
}

// 뷰 하나가 이번 프레임에 쓸 광원 목록과 그 근거.
struct ViewLightSelection
{
    // 기여도 내림차순. 패스는 앞에서부터 자기 한도만큼 가져간다.
    std::vector<EnhancedLight> lights;

    uint32_t sceneLights{ 0 };       // 켜져 있던 광원 수
    uint32_t culledByFrustum{ 0 };   // 뷰에 닿지 않아 뺀 수
};

// Retained by the producer or by one view execution slot. Scoring stores only
// indices; sorting does not repeatedly move the 64-byte shader payload. A caller
// must not share this scratch across concurrent selections.
struct ViewLightSelectionScratch
{
    struct Scored
    {
        size_t proxyIndex{};
        float score{};
        bool isDirectional{};
    };
    std::vector<Scored> scored;
};

// 카메라 절두체. 직교 투영에서는 만들지 않는다 — compact slope frustum 생성은
// 원근 전용이라 직교 행렬을 주면 표현할 수 없는 절두체가 나온다.
inline bool BuildViewFrustum(const FrameCameraSnapshot& camera,
    math::bounding_frustum& outFrustum)
{
    if (camera.isOrthographic) return false;

    const auto local =
        math::try_bounding_frustum_from_projection_lh(camera.projection);
    if (!local) return false;
    outFrustum = math::transform(*local, camera.inverseView);
    return true;
}

// 뷰가 쓸 광원을 고르고 기여도 순으로 세운다.
//
// ── 왜 순서가 중요한가 ──
//
//   패스마다 셰이더 배열 크기에서 오는 한도가 있다(Deferred 64 ·
//   VolumetricFog 20 · Forward+ 타일당 32). 예전에는 그 한도를 넘는 순간
//   **등록 순서로** 잘렸다 — 씬을 밝히는 태양이 나중에 등록됐다는 이유로
//   빠지고 구석의 약한 점광이 남을 수 있었다. 목록을 기여도 순으로 세우면
//   같은 한도에서 "앞의 N개"가 곧 "가장 중요한 N개"가 된다.
//
// ── 기여도 ──
//
//   방향광은 거리 개념이 없고 씬 전체를 밝히므로 언제나 앞에 둔다(그들끼리는
//   세기순). 점광·스포트는 카메라가 그 영향 구(球) 표면에서 얼마나 떨어져
//   있는지로 잰다:
//
//       d = max(0, |eye - pos| - range)
//       score = intensity / (1 + d*d)
//
//   카메라가 구 안에 있으면 d=0이라 세기 그대로가 되고, 멀어질수록 이차로
//   준다. 1을 더해 0 나눗셈을 피한다. 입력 세기가 NaN인 경우에도 정렬
//   비교가 일관되도록 아래 비교자는 NaN을 같은 광원 종류의 맨 뒤에 둔다.
//
// ★ 스포트도 구로 다룬다. 원뿔은 자기 구 안에 들어가므로 보수적이다 —
//   빠뜨리지는 않고 가끔 필요 없는 것을 남긴다. 원뿔로 좁히는 것은 이
//   목록의 길이가 실제로 문제가 될 때 할 일이다.
inline void SelectLightsForView(std::span<const own::shared_owner<LightRenderProxy>> proxies,
                                const FrameCameraSnapshot& camera, ViewLightSelection& selection,
                                ViewLightSelectionScratch& scratch)
{
    math::bounding_frustum frustum;
    const bool hasFrustum = BuildViewFrustum(camera, frustum);

    const math::vector3& eye = camera.eyePosition;

    selection.lights.clear();
    selection.sceneLights = 0;
    selection.culledByFrustum = 0;
    auto& scored = scratch.scored;
    scored.clear();
    scored.reserve(proxies.size());

    for (size_t index = 0; index < proxies.size(); ++index)
    {
        const auto& proxy = proxies[index];
        if (nullptr == proxy || !proxy->IsEnabled()) continue;

        ++selection.sceneLights;

        ViewLightSelectionScratch::Scored entry{};
        entry.proxyIndex = index;
        entry.isDirectional = proxy->IsInfiniteReach();

        if (entry.isDirectional)
        {
            entry.score = proxy->m_intensity;
        }
        else
        {
            // 영향 반경이 0이면 그릴 것이 없다. 프러스텀 검사에 맡기면
            // 반경 0인 구가 어쩌다 걸릴 수 있어 여기서 끊는다.
            if (proxy->m_range <= 0.f)
            {
                ++selection.culledByFrustum;
                continue;
            }

            if (hasFrustum)
            {
                const math::sphere reach{
                    proxy->m_worldPosition, proxy->m_range };

                if (!math::intersects(frustum, reach))
                {
                    ++selection.culledByFrustum;
                    continue;
                }
            }

            const float distance = math::distance(proxy->m_worldPosition, eye);
            const float surfaceDistance = (std::max)(0.f, distance - proxy->m_range);

            entry.score = proxy->m_intensity /
                (1.f + surfaceDistance * surfaceDistance);
        }

        scored.push_back(entry);
    }

    // Original ordinal preserves equal-score order without stable_sort's
    // temporary allocation. NaN scores sort last within their light-type group;
    // treating NaN as equal to every finite score would break strict ordering.
    std::sort(scored.begin(), scored.end(),
              [](const ViewLightSelectionScratch::Scored& a, const ViewLightSelectionScratch::Scored& b)
              {
                  if (a.isDirectional != b.isDirectional)
                      return a.isDirectional;
                  const bool aNan = std::isnan(a.score);
                  const bool bNan = std::isnan(b.score);
                  if (aNan != bNan)
                  {
                      return !aNan;
                  }
                  if (a.score > b.score)
                  {
                      return true;
                  }
                  if (a.score < b.score)
                  {
                      return false;
                  }
                  return a.proxyIndex < b.proxyIndex;
              });

    selection.lights.reserve(scored.size());
    for (const auto& entry : scored)
    {
        selection.lights.push_back(MakeEnhancedLight(*proxies[entry.proxyIndex]));
    }
}

// Convenience for one-shot consumers. The live renderer supplies retained
// output/scratch through the overload above to avoid allocating per view.
inline ViewLightSelection SelectLightsForView(const std::vector<own::shared_owner<LightRenderProxy>>& proxies,
                                              const FrameCameraSnapshot& camera)
{
    ViewLightSelection selection;
    ViewLightSelectionScratch scratch;
    SelectLightsForView(proxies, camera, selection, scratch);
    return selection;
}
