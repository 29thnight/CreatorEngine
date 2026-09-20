# CreatorRobot 로컬 회귀 모델

Gunner를 지정하던 애니메이션·모델 배치·재로드·PBR 게이트는
`Dynamic_CPP/Assets/Models/CreatorRobot.glb`를 사용한다.

- 오브젝트 배율 `(1, 1, 1)`, 기준 자세 키 1.8 엔진 유닛(미터).
- skinned mesh primitive 4개, 재질 4개, 내장 텍스처 12개.
- 재질당 BaseColor / Normal / MetallicRoughness / Occlusion 텍스처 속성 4개,
  총 16개 참조. ORM을 두 속성이 공유하므로 고유 텍스처 owner는 12개다.
- 클립 8개: Idle, Walk, Run, Jump, FallLoop, Land, Hit, Death.
- 변형 관절 53개. 엔진 skeleton에는 상위 Armature 노드를 포함해 54개가 등록된다.
- 기본 클립 0은 Idle이다. 시각 회귀는 이름으로 Walk와 Run을 찾아 사용한다.
- 시각 회귀의 손 소켓 표시는 로봇의 붉은 재질과 구분되는 초록 큐브다.

이 GLB는 변하지 않는 관절 translation/scale 키를 생략한다. glTF 임포터는
애니메이션 채널에서 빠진 TRS 성분을 해당 node의 기준값으로 채워야 한다.
그렇지 않으면 관절이 원점으로 모인다. 실제 GPU 시각 회귀는 이 상태에서
블렌드·상체 마스크의 몸체 픽셀 변화 검사가 실패하는 것을 확인했으며,
임포터 수정 후에도 같은 판정 기준을 유지한다.

## 준비와 임포트

`Tools/blender/README-starter-robot.md`의 Blender 작업으로 8종을 준비한다.
`model.load <준비 폴더>/CreatorRobot.glb`는 에디터의 정식 임포트 경로를 통해
Assets/Models에 복사하고 identity sidecar와 cooked generation을 생성한다.
모델 바이트만 복사하거나 sidecar GUID를 직접 작성하지 않는다.

애니메이션 재생: `verify-animation-product.ps1`.
GPU 이미지·블렌드·상체 마스크·소켓: `verify-animation-visual.ps1`.
cold reload·재질 참조: `verify-model-scene-consumption.ps1`.
드롭 및 취소·undo/redo: `verify-editor-drop-animation.ps1`,
`verify-model-async-placement.ps1`.
PBR: `verify-pbr-wiring-baseline.ps1`, `verify-pbr-soak.ps1`.

GLB는 반복·상태 전이의 엔진 저작 설정을 대신하지 않는다. Jump / Land / Hit /
Death를 게임에 연결할 때는 Animator에서 단발 재생으로 설정하고 Death는 마지막
자세를 유지한다. 테스트는 각 검증에 필요한 재생 설정을 명시한다.

## 출처 및 저장소 범위

모델·텍스처: **Sci-Fi Humanoid Robot**, **yur1_val**.
원본: https://skfb.ly/p7Y9Z
모델 페이지: https://sketchfab.com/3d-models/sci-fi-humanoid-robot-12ad786ecde246e5854a61fd4f67ed49
라이선스: https://creativecommons.org/licenses/by/4.0/
변경: 단위·방향 정규화, 경량화, 53본 리그·가중치, 텍스처 축소·ORM 구성,
노멀 방향 변환, 대상 리그 애니메이션 작성·리타게팅.

Idle / Walk / Run / Hit / Death는 SU_Mythic 유래이며 모델의 CC BY 라이선스가
적용되지 않는다. 재배포 권리가 확인되기 전에는 이 8종 포함본을 로컬 검증용으로
취급한다. 따라서 모델과 sidecar는 기존 Assets/Models ignore 규칙을 유지하며,
clean checkout에 포함된 fixture라고 주장하지 않는다. 모델이 필요한 게이트는
파일 부재를 실패로 알리고, PBR 종합 게이트는 해당 축을 skipped로 기록한다.

과거 측정 결과와 corpus baseline에 남아 있는 Gunner 이름·수치는 당시의 기록이다.
이를 새 모델의 측정값으로 바꾸지 않는다.

## 검증 기록 (2026-09-20, DX12)

- Blender/GLB 검증 1,208개 통과. 준비본과 엔진 Assets의 GLB SHA-256 일치.
- Editor Debug/Release 빌드 통과.
- 애니메이션 제품 검사: 100개 Animator, 12회, 69,514개 단정 통과.
- Debug/Release GPU 시각 검사: 구성별 13개 캡처, 264개 단정 통과.
  임포터 수정 전에는 관절 translation 소실로 몸체 픽셀 변화 3개 단정이 실패했다.
  수정 후 동일한 기준으로 통과했으며 임계값을 낮추지 않았다.
- 에디터 드롭: Play 중 재생 시간·팔레트 변화와 제품 포즈 게시 확인.
- 비동기 배치: cold 준비, 점진 배치, undo/redo, 실패·씬 전환·Play 취소 통과.
- 별도 프로세스 씬 로드 및 중복 reload: 로봇 renderer 4개, texture 참조 16개,
  고유 texture 12개 유지. 전체 씬 GPU mesh 업로드 12/12, 해석 실패 0.
- Release PBR 제품 화면 검사 통과 (`-ProductOnly`, 로봇 포함).
- Release 짧은 반복 검사: 진행 프레임 102개, 재임포트 4회,
  서로 다른 세대가 함께 그려진 표본 83개, 실제 실행 27초.

이번 검증은 Vulkan, 전체 PBR 격리 검사, 10분 장시간 검사 범위를 포함하지 않는다.
원시 결과와 실제 GPU 캡처는 `Build/RobotImport/`에 보관했다.
`Visual-TRS-Debug/` 및 `Visual-TRS-Release/animation-contact-sheet.png`에서
걷기·달리기·블렌드·상체 마스크·소켓 결과를 확인할 수 있다.
