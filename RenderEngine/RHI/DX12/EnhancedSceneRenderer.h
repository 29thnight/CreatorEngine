#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <string>

// DX12 렌더러(PHASE 3-3). 기존 SceneRenderer(DX11)와 병존하다가 3-9에서 교체된다.
//
// 지금은 브링업 골격이다: 자체 디바이스·큐·펜스·PSO로 프레임을 그려 파일로
// 증명하는 자가 검증만 가진다. 씬 통합(렌더그래프 실행)은 3-5/3-6에서 이 위에
// 얹힌다 — 골격이 검증되기 전에 씬을 태우면 실패의 원인 분리가 안 된다.
class EnhancedSceneRenderer
{
public:
    // 브링업 자가 검증. frameCount 프레임을 그려 얼로케이터 회전(3개 × 2회전 이상
    // 권장)과 펜스 동기화를 검증하고, 마지막 프레임을 PNG로 저장한다.
    //
    // 통과 조건(전부 만족해야 true):
    //   1) 디바이스·큐·PSO 생성 성공   2) frameCount 프레임 제출·완료
    //   3) 리드백 픽셀이 기대 내용(배경 클리어 + 삼각형)   4) 검증 레이어 메시지 0건
    // 진행 로그는 outLog로 — 실패 지점이 어디인지가 곧 진단이다.
    bool RunSelfTest(const std::string& outputPngPath, uint32_t frameCount, std::string& outLog);
};

#endif
