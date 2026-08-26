#pragma once
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
#include "../../MathematicsInterop.h"

#include <DirectXCollision.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

inline EnhancedLight MakeEnhancedLight(const LightRenderProxy& source)
{
    EnhancedLight light{};

    light.position = math::vector4{
        source.m_worldPosition.x, source.m_worldPosition.y, source.m_worldPosition.z,
        static_cast<float>(source.m_lightType) };

    light.direction = math::vector4{ source.m_direction.x, source.m_direction.y,
        source.m_direction.z, math::radians(source.m_spotLightAngle) };

    light.color = source.m_color;
    light.color.w = source.m_intensity;

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

// 카메라 절두체. 직교 투영에서는 만들지 않는다 — CreateFromMatrix가
// 원근 전용이라 직교 행렬을 주면 쓸 수 없는 절두체가 나온다.
inline bool BuildViewFrustum(const FrameCameraSnapshot& camera,
    DirectX::BoundingFrustum& outFrustum)
{
    if (camera.isOrthographic) return false;

    DirectX::BoundingFrustum::CreateFromMatrix(
        outFrustum, MathematicsInterop::ToDirectX(camera.projection));
    outFrustum.Transform(
        outFrustum, MathematicsInterop::ToDirectX(camera.inverseView));
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
//   준다. 1을 더하는 것은 0 나눗셈을 피하려는 것이고, 덕분에 점수가 유한해
//   비교가 안전하다.
//
// ★ 스포트도 구로 다룬다. 원뿔은 자기 구 안에 들어가므로 보수적이다 —
//   빠뜨리지는 않고 가끔 필요 없는 것을 남긴다. 원뿔로 좁히는 것은 이
//   목록의 길이가 실제로 문제가 될 때 할 일이다.
inline ViewLightSelection SelectLightsForView(
    const std::vector<std::shared_ptr<LightRenderProxy>>& proxies,
    const FrameCameraSnapshot& camera)
{
    struct Scored
    {
        EnhancedLight light{};
        float         score{ 0.f };
        bool          isDirectional{ false };
    };

    DirectX::BoundingFrustum frustum;
    const bool hasFrustum = BuildViewFrustum(camera, frustum);

    const math::vector3& eye = camera.eyePosition;

    ViewLightSelection selection;
    std::vector<Scored> scored;
    scored.reserve(proxies.size());

    for (const auto& proxy : proxies)
    {
        if (nullptr == proxy || !proxy->IsEnabled()) continue;

        ++selection.sceneLights;

        Scored entry{};
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
                const DirectX::BoundingSphere reach{
                    DirectX::XMFLOAT3{ proxy->m_worldPosition.x,
                                       proxy->m_worldPosition.y,
                                       proxy->m_worldPosition.z },
                    proxy->m_range };

                if (!frustum.Intersects(reach))
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

        entry.light = MakeEnhancedLight(*proxy);
        scored.push_back(entry);
    }

    // ★ stable_sort다. 점수가 같은 광원들의 순서가 프레임마다 뒤집히면
    //   한도 경계에 걸린 것이 깜빡인다 — 투명 정렬에서 같은 이유로
    //   stable_sort를 쓴 것과 같은 부류다.
    std::stable_sort(scored.begin(), scored.end(),
        [](const Scored& a, const Scored& b)
        {
            if (a.isDirectional != b.isDirectional) return a.isDirectional;
            return a.score > b.score;
        });

    selection.lights.reserve(scored.size());
    for (const Scored& entry : scored)
    {
        selection.lights.push_back(entry.light);
    }

    return selection;
}
