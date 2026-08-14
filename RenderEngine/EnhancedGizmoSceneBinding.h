#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <vector>

#include "Render/Passes/Editor/EnhancedGizmoIconPass.h"
#include "Render/Passes/Editor/EnhancedGizmoLinePass.h"
#include "FrameCameraSnapshot.h"

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

// 한 프레임의 기즈모 씬 입력. 게임 자료구조를 들지 않는다 —
// 아이콘 텍스처 포인터는 수명이 씬보다 긴 전역 아이콘(DataSystems)이다.
struct EnhancedGizmoSceneData
{
    std::vector<EnhancedGizmoIconPass::Icon> icons;

    uint32_t cameraIcons{ 0 };
    uint32_t lightIcons{ 0 };
    uint32_t selectionShapes{ 0 };
    uint32_t colliderShapes{ 0 };
};

/// 활성 씬에서 기즈모 입력을 수집한다.
///
/// 아이콘은 out.icons로, 선(선택 기즈모·콜라이더)은 linePass에 직접
/// 쌓는다(ResetLines는 호출부 몫 — 주입 내용과 섞을 수 있게).
///
/// collectColliders: DX11에서는 디버그 모드일 때만 수집한다. 판정
/// (EngineSettingInstance)은 호출부가 내리고 여기는 시키는 대로 한다 —
/// 검증이 모드와 무관하게 경로를 태울 수 있어야 하기 때문이다.
///
/// 활성 씬이 없으면 false.
bool BuildEnhancedGizmoSceneData(const FrameCameraSnapshot& snapshot,
    bool collectColliders, EnhancedGizmoLinePass& linePass,
    EnhancedGizmoSceneData& out);

/// 콜라이더 와이어를 수집할지의 엔진 판정(DX11 GizmoLinePass와 같은 기준).
///
/// 이 함수가 있는 이유는 계층이다: EngineSetting은 EngineEntry 소속이라
/// RHI/DX12의 cpp가 직접 인클루드하면 그쪽 헤더 스택(RenderPassSettings 등)이
/// 풀리지 않는다. 루트의 이 번역 단위는 이미 컴포넌트·설정 계층을 여는
/// 자리이므로(위 ★ 주석), 판정도 여기서 내려 준다.
bool ShouldCollectGizmoColliders();

#endif
