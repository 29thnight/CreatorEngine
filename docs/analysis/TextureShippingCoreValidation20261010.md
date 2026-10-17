# PHASE 12 핵심 파이프라인 검증 — 2026-10-10

## 판정

Release Shipping의 실제 DX12 패키지 실행을 **독립 PNG/HDR 원본이 없는 범위**에서 수용한다. Inspector 배율/도킹 후속은 사용자 요청으로 보류했다. 전체 PHASE 12 완료나 모든 cooked texture의 픽셀 정확도 수용은 아니다.

현재 worktree HEAD `d3c85faf182e93842217dd46eee3d3aabf4afc13` 기준 Release AssetCooker/AssetPacker와 Shipping Player를 빌드했다. 제품 수정 후 최신 실행은 `Build/Obj/MaterialProductProbe/ScenePkg-Release-Shipping-cea0ff2f5dee4299a4b3db65ec23e7aa`이다. 통합 영수증은 `Build/Phase12CoreVerification/result.json`, 실행 로그는 `Build/phase12-sourcefree-shipping-package-final-20261010.log` 및 fixture의 `package.log`다.

## 재현한 결함과 수정

1. 오래된 AssetPacker API35와 현재 API40의 배포 조합이 거부됐다. Release AssetPacker를 재빌드해 일치시켰다. 소스 API 결함으로 분류하지 않는다.
2. 배포 Slang DLL 경로258자에서 일반 LoadLibrary가 오류206을 반환했다. 동일 파일의 확장 절대 경로는 로드 성공/오류0이었다(`Build/phase12-slang-longpath-reproduction.json`). RHIShaderCompiler가 Slang/dxil/dxcompiler의 고정 번들 경로를 확장 절대 경로로 로드하도록 수정했다. 짧은 경로로 우회하지 않고 같은 깊이의 배포 cook을 통과했다.
3. 최초 Shipping smoke 성공(`ScenePkg-Release-Shipping-fc0580abc3574a4ebba4723346458f09`)에도 fixture PNG와 HDR 원본이 PAK에 남았다. 이 실행을 원본 부재 수용에서 제외한다. BuildTool은 성공한 native cook의 선택 목록을 보존하고 runtime document 생성 뒤 소유한 패키지 입력 복사본에서 그 원본/sidecar만 제거한다. 프로젝트 원본·선택되지 않은 이미지·CECT는 유지한다. BuildTool148 checks에 원본 보존·선택 목록·경로 탈출·누락 거부6개가 포함된다.
4. 제거 후 실제 Shipping 실행(`ScenePkg-Release-Shipping-68090902b6344a3e9932526d88b965bd`)은 exit161, `CEMF source asset이 package에 없다: fixture.png`로 실패했다. DataSystem의 catalog mount가 CEMF Texture2 및 존재/크기가 일치하는 artifact로 원본 없는 texture identity를 허용하도록 수정했다. 타입별 취득의 payload 검증은 유지한다. mount만으로 전체 digest 검증을 수용하지 않는다.

## 최신 실행 근거

명령: `Tools/regression/verify-material-scene-package.ps1 -Configuration Release -Shipping -SmokeTimeoutSeconds 600`.

- Shipping=true, exit0; GT1630/display1631, 실제 submittedGameFrames228, promotions120.
- DX12 DebugLayer/GPUValidation 활성화. cooked Scene program 준비 완료, sceneCompiles0, textParserCalls0, cookedSceneDocuments1. Shipping 서비스 compiled=no/enabled=no.
- 독립 texture2·model1·material program3·material1 cook. PAK의 standalone 원본 이미지 확장자 항목0, DirectXTex/stb_image 이름의 runtime 파일0.
- runtime234개와 PAK SHA 모두 실행 후 manifest 일치, Shipping Player.runtime.dll SHA는 현재 Bin과 일치. 소스761개 변경0. Presentation/Render 종료 pending0·balanced1, DX12 종료 큐0/0/0.
- 회귀 판정기에 실제 구성 보고, Shipping 인자/런타임 일치, 원본 이미지 부재 검사를 추가했다.

별도 Release cooked Scene 회귀는24frames/57259checks/GPU components10746/validation0, 반복 cook bytes 동일·실패 입력6개 거부·기존 산출물 보존을 통과했다(`Build/phase12-core-cook-20261010.log`). 이 GPU 비교기의 texture 입력은 합성 이미지이며 encrypted PAK에서 취득한 것은 cooked program이다. **CECT payload의 GPU sampling 비교로 계상하지 않는다.** 이 실행은 loader/mount 수정 전 회귀다. 당시 최종 출력의 Debug,Release 표기는 하드코딩 오류였으며 실제 실행 구성은 Release 하나다. 스크립트 보고를 실제 구성 목록으로 수정했다.

## 미수용 경계와 다음 작업

PAK에 Prim_Plane.glb 원본은 남는다. 모델 원본 제거·모든 모델 texture 역할 소비, 새 BC5/BC7의 실제 CECT payload를 사용하는 DX12/Vulkan GPU 픽셀 비교, 전체 소비자 행렬은 잔여다. 수정 후 missing/truncated CECT 실패 주입, Debug 제품 재빌드/동일 실행도 이 결과에는 포함하지 않는다. Shipping 프레임 수는 실행 증거이며 성능/품질 측정이 아니다. 다음 우선순위는 CECT payload의 실제 GPU sampling과 실패 주입이다.

기존 T2 잔여 수용 단위를 전부 닫지 않았으므로 추가 가중치 없이13.25/20일=66.25%를 유지한다. T1b 품질/시간/메모리 대기와 T3 중단도 유지한다.
