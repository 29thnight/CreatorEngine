# PHASE 12 Shipping CECT 실패 입력 — 2026-10-10

## 수용 범위

최신 Release Shipping Player에서 **PNG/HDR 독립 CECT의 누락·잘림4개를 catalog mount 단계에서 거부**하고, 같은 fixture의 원본 artifact 복원 후 정상 DX12 렌더/종료를 한정 수용한다. 제품 수정·추가 빌드는 없다. 핵심 검사기는 `Tools/regression/verify-texture-shipping-artifact-failure.ps1`이다.

기준 정상 패키지는 §34의 `ScenePkg-Release-Shipping-cea0ff2f5dee4299a4b3db65ec23e7aa/Stage/Game-864cc3d45c3347138f5e347d065a95e1`이다. 이전 실패 candidate의 보존된 Merged 입력을 **새 소유 fixture로 복사**하고 현재 Shipping runtime을 별도 복사했다. 원본 이미지 없이 AssetPacker로 다시 암호화한 PAK를 실제 Player가 추출/마운트한다. 기존 정상 패키지를 수정하지 않는다.

실행:

```powershell
Tools/regression/verify-texture-shipping-artifact-failure.ps1 -ContentRoot 'Build/Obj/MaterialProductProbe/ScenePkg-Release-Shipping-68090902b6344a3e9932526d88b965bd/Stage/.1c8f577ab2ae4be6907124bca3624960.candidate/.package-input/Merged'
```

결과는 `Build/Obj/TextureShippingFailure/029b8d97524d4ab5bd8c387c161919e1/result.json`과 개별 pack/stdout/stderr 로그에 있다. 최신 결과 위치는 `Build/Phase12CoreVerification/artifact-failure-root.txt`에 기록했다.

| 입력 | 실제 결과 |
|---|---|
| fixture PNG CECT 삭제 | exit161, mount 실패 identity `fixture.png` |
| fixture PNG CECT 4바이트로 잘림 | exit161, 같은 identity |
| HDR CECT 삭제 | exit161, mount 실패 identity `HDR/kloofendal_43d_clear_puresky_4k.hdr` |
| HDR CECT 4바이트로 잘림 | exit161, 같은 identity |
| 두 artifact 원본 bytes 복원 | exit0, GT1581·submitted202·promotions121·ready=true |

복원 실행은 GPUValidation 활성화, cooked Scene/program 준비 완료·sceneCompiles0, textParserCalls0, Shipping service compiled=no/enabled=no, Presentation/Render pending0·balanced1 및 DX12 종료 큐0/0/0을 통과했다. 초기 material 준비 대기는 이후 ready/실제 제출로 해소됐다. 프레임 수를 성능 수용으로 사용하지 않는다.

각 실행 PAK SHA가 실행 전후 동일했다. 정상 패키지 runtime234개+PAK 총235파일과 원래 Merged278파일 SHA를 전부 보존했다. 복사된 테스트 runtime도 기준 manifest와 일치했다. 현재 Shipping DLL SHA는 `30f932641f6df7d6ef41ebb6f2a7bb9bf4308be35dfdd02ddaaf8d14865535dc`다.

실행 중 정상 복원 판정기를 readiness/제출/promotion/Shipping/parser/종료 balance까지 강화했다. 완료 로그에 동일한 강화 조건을 다시 적용해 통과한 receipt를 보존했다. 검사기 PowerShell parse를 통과했다.

## 남은 경계

동일 크기 payload 손상은 mount의 존재/크기 검사로 판단할 수 없으며 typed acquisition/실제 소비 검증이 필요하다. 이4개는 해당 손상 검증이나 BC5/BC7 CECT GPU 픽셀 oracle 수용이 아니다. 전체 소비자·모델 원본 제거/texture 역할·Vulkan·Debug 제품 행렬도 남는다. 추가 가중치 없이66.25%를 유지한다.
