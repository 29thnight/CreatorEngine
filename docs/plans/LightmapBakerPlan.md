# 라이트맵 베이커 구현 계획과 현재 메타데이터

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 구현된 것과 비어 있는 것

LightMapping에는 lightmapIndex, 기존 철자의 ligthmapResolution, lightmapScale, offset/tiling 값이 있다. MeshRenderer에서 proxy로 값이 전달된다. 이 직렬화/프록시 표면만으로 UV 생성·bake dispatch·광선 탐색·완성 texture 저장·제품 sampling까지 구현되었다고 할 수 없다. 감사한 live pipeline에는 완성된 lightmap baker의 진입·실행 배선이 없다.

현재 ProxyCommand의 갱신은 유효한 lightmapIndex와 index 변경을 조건으로 삼는다. 같은 index의 scale/offset만 변경한 경우까지 완전한 실시간 반영 계약으로 가정하지 않는다. 새 baker는 저장 값과 실제 shader 소비를 끝까지 연결해야 한다.

## 자산·UV 저작

bake 대상으로 선택한 geometry만 UV1을 생성하고 seam에 필요한 vertex split을 수행한다. mesh/material·skin/remap 의미를 유지한 새 불변 모델 generation으로 원자 게시한다. source geometry를 Player에서 몰래 바꾸지 않는다. surface area와 lightmapScale로 texel density를 정하고 실제 chart padding·atlas rect·최대 해상도를 명시한다. xatlas 도입은 별도 vendor/version/license 확인 뒤 원본 유지 원칙으로 수행한다는 미래 요구다.

## 탐색과 dispatch

CPU BVH는 SAH 또는 longest-axis 분할과 nth_element를 평가하되 comparator에 쓰기를 넣지 않는다. half-open leaf 범위·동일 중심·퇴화 삼각형·NaN·빈 mesh를 oracle과 mutation 검사로 검증한다. DXR 사용은 capability와 전체 build/trace 비용을 근거로 선택하며 필수 구현으로 가정하지 않는다.

GPU dispatch는 실제 chart/rect와 유효 light 수에 한정한다. 전체 atlas를 매 batch 무조건 훑는 구현을 성능 완료로 보지 않는다. progressive indirect bounce, denoise·dilate·padding의 공간 의미를 분리하고 각 단계의 중간 결과를 재개 가능하게 보존한다.

## Editor 작업과 수명

Editor background worker가 sealed geometry·material·lighting snapshot을 소비한다. 게임 스레드 lockstep으로 장시간 bake를 실행하지 않는다. bounded dispatch와 GPU time budget, cancel·pause/resume checkpoint를 두고 입력 변경 시 해당 작업을 invalidation한다. 전용 compute queue는 Q0 계약 소비자로 검토할 수 있지만 RG8 자동 scheduler 전체가 필수 선행은 아니다. overlap 이득은 실제 측정해야 한다.

완성 결과는 texture/artifact generation과 resolver를 통해 원자 게시하고 실제 mesh/lightmap shader binding까지 검증한다. 취소·실패·새 세대 게시가 기존 정상 자산을 손상하지 않아야 한다. old snapshot·GPU 중간 자원은 마지막 사용 완료 전 회수하지 않는다.

## 수용과 산정

UV 겹침/범위·밀도, BVH hit oracle, direct/indirect 에너지·누수·seam, 실제 atlas sampling, 재로드·source-free cook·입력 변경·취소/재개·예산을 검사한다. 단순 메타데이터 왕복이나 빈 dispatch 성공은 완료가 아니다. 기존 L0~L7 2/5/4/6/7/5/3/3인일, 총 35인일은 미구현 범위의 계획 추정이며 RT/queue 선택 후 재산정한다.

## 소스 근거

- [Engine/RenderEngine/Interfaces/LightMapping.h](../../Engine/RenderEngine/Interfaces/LightMapping.h)
- [Engine/SceneRuntime/ProxyCommand.cpp](../../Engine/SceneRuntime/ProxyCommand.cpp)
- [Engine/SceneRuntime/PrimitiveProxyBridge.cpp](../../Engine/SceneRuntime/PrimitiveProxyBridge.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/RHI/RHIQueueContract.h](../../Engine/RenderEngine/RHI/RHIQueueContract.h)
