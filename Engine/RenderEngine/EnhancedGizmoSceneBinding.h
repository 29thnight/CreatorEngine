#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "EnhancedGizmoSceneTypes.h"
#include "FrameCameraSnapshot.h"

class Texture;

// Host가 제공하는 Editor gizmo 그림 묶음. Render/Script binding은 경로나
// Editor 객체를 모르고, 프레임을 밀봉할 때 이 값 입력만 받는다.
struct EnhancedGizmoIconTextures
{
    std::shared_ptr<Texture> camera;
    std::shared_ptr<Texture> mainLight;
    std::shared_ptr<Texture> directionalLight;
    std::shared_ptr<Texture> pointLight;
    std::shared_ptr<Texture> spotLight;
};

// Gizmo 계열 씬 연결 (PHASE 3-6, Gizmo 계열 5차 슬라이스).
//
// 활성 씬에서 기즈모 입력(아이콘 대상·선택 기즈모·콜라이더 와이어)을
// 밀봉 복사해 DX12 패스가 먹는 형태로 만든다. DX11에서는 이 수집이
// GizmoPass/GizmoLinePass의 Execute 안에 있었다 — 렌더 스레드가 게임
// 자료구조를 직접 읽는 구조였고, 그것이 3-0/3-2가 걷어낸 문제 부류다.
// 여기서는 수집을 프레임 밀봉 시점(게임 스레드 쪽)의 함수로 분리한다.
//
// ★ 이 파일이 RHI/DX12가 아니라 RenderEngine 루트에 있는 이유:
//   컴포넌트 헤더(ScriptBinder의 CameraComponent.h 등)는 "Camera.h" 같은
//   루트 헤더를 인클루드 스택으로 찾는다 — 루트의 cpp에서만 풀린다.
//   DX11 패스들이 컴포넌트를 읽을 수 있던 것도 같은 이유다.

// 한 프레임의 기즈모 씬 입력. 게임 자료구조도, 에디터 패스 타입도 들지
// 않는다(E4-3a — 값 타입의 정본은 EnhancedGizmoSceneTypes.h) — 아이콘의 raw
// pointer는 패스 입력 형식이고, 아래 shared iconTextures가 그 포인터의 수명을
// RenderThread 소비 완료까지 붙든다.
struct EnhancedGizmoSceneData
{
    std::vector<EnhancedGizmoIcon> icons;
    std::vector<EnhancedGizmoLineVertex> lineVertices;
    std::shared_ptr<const EnhancedGizmoIconTextures> iconTextures;

    uint32_t cameraIcons{ 0 };
    uint32_t lightIcons{ 0 };
    uint32_t selectionShapes{ 0 };
    uint32_t colliderShapes{ 0 };
};

/// 활성 씬에서 기즈모 입력을 수집한다.
///
/// 아이콘은 out.icons로, 선(선택 기즈모·콜라이더)은 lineCollector에 직접
/// 쌓는다(Reset은 호출부 몫 — 주입 내용과 섞을 수 있게). 수집기가 에디터
/// 패스가 아니라 Core 타입인 것이 E4-3a의 요점이다 — GT 수집이 표시 계층을
/// 인스턴스화하지 않는다.
///
/// collectColliders: DX11에서는 디버그 모드일 때만 수집한다. 판정은
/// 호출부가 내리고 여기는 시키는 대로 한다 —
/// 검증이 모드와 무관하게 경로를 태울 수 있어야 하기 때문이다.
///
/// 활성 씬이 없으면 false.
bool BuildEnhancedGizmoSceneData(const FrameCameraSnapshot& snapshot,
    bool collectColliders,
    std::shared_ptr<const EnhancedGizmoIconTextures> iconTextures,
    EnhancedGizmoLineCollector& lineCollector,
    EnhancedGizmoSceneData& out);

/// Game-thread packet capture convenience. Geometry is accumulated into owned
/// vertices so the consumer never has to revisit Scene/Entity state.
bool CaptureEnhancedGizmoSceneData(const FrameCameraSnapshot& snapshot,
    bool collectColliders,
    std::shared_ptr<const EnhancedGizmoIconTextures> iconTextures,
    EnhancedGizmoSceneData& out);

/// Presentation thread에서 디버그 콜라이더 수집을 켜고 끈다. Game thread의
/// packet capture는 아래 atomic 판정만 읽어 에디터 설정 계층에 의존하지 않는다.
void SetCollectGizmoColliders(bool enabled) noexcept;
bool ShouldCollectGizmoColliders() noexcept;

