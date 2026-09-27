# 메모리 프로파일러 확장

2026-09-23 현재 구현과 Unity Memory Profiler 수준으로 가기 위한 계측 경계를 기록한다. 이 트랙은 PHASE 14 P5의 프레임별 counter 수집과 별개다. 프레임 그래프는 시간 추이를, 이 트랙의 스냅샷은 한 시점의 객체·주소·힙 구성을 설명한다.

## 현재 계약

- Memory 탭의 **스냅샷 촬영**은 다음 GameThread 경계에서 한 번만 실행한다. 에디터 표시 스레드는 `DataSystem`이나 CoreCLR을 직접 읽지 않는다.
- OS `WorkingSetSize`와 `PrivateUsage`, Debug CRT의 살아 있는 블록 합계, CoreCLR 관리 힙·단편화, 렌더 백엔드의 마지막 VRAM 표본을 별도 지표로 표시한다. 이 값들은 중첩되므로 합산하지 않는다.
- `VirtualQuery`의 commit private/image/mapped와 reserve를 주소 지도에 싣는다. 이는 가상 주소 영역이며 물리 상주량이나 할당 소유자가 아니다.
- `DataSystem`의 현행 모델·재질·텍스처 캐시를 객체 목록으로 읽는다. 텍스처 CPU 픽셀은 보존된 subresource의 `slicePitch`를 세고 동일 픽셀 주소는 한 번만 센다. 모델 descriptor의 `byteSize`는 GPU 업로드 요청량으로만 표시한다.
- 최근 8개 스냅샷을 메모리에 두고 A/B 요약·객체 차이를 비교한다. Summary, Engine Objects, All of Memory, Memory Map을 Memory 탭 안에 둔다. Summary에는 관리 힙 단편화와 엔진 자산 범주를 표시하고, Map에는 큰 커밋 영역의 면적 지도와 상세 표를 둔다. 기존 RAM 시간 그래프는 Summary에 남긴다.
- `memory.capture` / `memory.snapshot`은 UI 없이 같은 수집 경계를 검증한다.

## 진행 순서와 완료 조건

| 단계 | 상태 | 완료 조건 |
|---|---|---|
| MP0 — 수동 스냅샷 경계와 정확한 출처 표기 | 완료 | GameThread에서 요청당 한 번 수집, 다른 프레임의 두 요청 구별, OS·CRT·GC·VRAM·주소 영역의 유효성 표시, 출처가 다른 값의 합계 금지 |
| MP1 — 스냅샷 탐색 화면 | 완료 | Summary·Engine Objects·All of Memory·Memory Map 및 A/B 비교, 신규·삭제 객체 모두 표시, 큰 주소 영역 시각화, 실제 에디터 화면 조작 확인 |
| MP2 — 네이티브 할당 소유자 | 미착수 | 엔진 allocator와 리소스 소유자에 태그를 넣고 블록·크기·생애를 스냅샷으로 보존. CRT 합계만으로 객체별 native memory를 추정하지 않음 |
| MP3 — 관리 객체 그래프 | 미착수 | CoreCLR의 객체 형식·크기·루트·참조를 일관된 정지 시점에 수집하고 강한 참조 경로와 도달 불능 객체를 탐색. 현행 `GC.GetGCMemoryInfo` 집계만으로 대체하지 않음 |
| MP4 — GPU 개별 리소스 | 미착수 | DX12/Vulkan 텍스처·메시·렌더 타깃·힙의 할당 바이트, 소유 객체, residency/예산을 RenderThread에서 발행. 어댑터 전체 VRAM 표본을 엔진 자산 사용량이라고 부르지 않음 |
| MP5 — 파일과 장기 비교 | 미착수 | 버전·길이·무결성 검사가 있는 스냅샷 저장/재열람, A/B 비교, 변경 객체 목록, 큰 스냅샷을 UI 중단 없이 처리 |
| MP6 — 제품 검증 | 미착수 | 실제 자산/씬과 관리 객체 자극, 스냅샷 전후 프레임 지연·메모리 자체 비용, Debug/Release·DX12/Vulkan·손상 파일 게이트 및 UI 화면 확인 |

공수는 계측 API와 런타임 정지 정책을 조사하기 전이라 모든 단계 `days: null`이다. 미착수 단계를 완료율의 완료 항목으로 세지 않는다.

## 검증 기록

- 2026-09-23 Debug `CreatorEditor.vcxproj` 전체 빌드 통과(초기 스냅샷 구현). 이후 전체 빌드는 별도 `RenderTests` 유니티 묶음의 C1128(`/bigobj`)로 중단됐다. 메모리 변경이 속한 `Editor.vcxproj`와 `CreatorEditor.vcxproj`를 의존 프로젝트 재빌드 없이 각각 성공시켜 최신 실행 파일을 링크했다. 링크 시 기존 Vulkan delay-load LNK4229 경고가 남았다.
- `Invoke-ProfilingValidation.ps1 -Action Memory`의 첫 실행은 OS/주소 두 스냅샷을 통과했지만 자산 0개였다. 애니메이션 워커 fixture는 모델·텍스처를 적재하지 않아 객체 수 검증에 부적합했다. 추적되는 `Prim_Cube.glb`를 직접 로드한 실행에서 frame 64→71, 객체 1→1, 주소 영역 1863→1863, 수집 18.69/21.69ms로 통과했다.
- 최신 실행 파일의 `Build/Validation/MemoryProfiler/20260923/final` 게이트는 frame 64→71, 자산 객체 1→1, 주소 영역 1873→1873, Debug CRT live 블록 16167→16177, 수집 25.04/22.53ms로 통과했다. 해당 commandlet은 렌더 백엔드를 구동하지 않아 VRAM 표본은 unavailable이다. 관리 런타임은 유효하나 이 자극의 관리 힙은 0바이트였다.
- 같은 실행 파일의 `Build/Validation/MemoryProfiler/20260923/window-smoke` 창 게이트는 열기·닫기·재열기와 ProfilerWindow/ProfilerTimeline 구간을 통과했다.
- 실제 에디터 창에서 Memory 선택, Summary/Engine Objects/Memory Map 전환, 두 번째 스냅샷 촬영과 자동 B 비교를 확인했다. 그림은 `Build/Validation/MemoryProfiler/20260923/ui-probe/window-memory.png`, `window-map.png`, `window-objects.png`, `window-compare2.png`에 있다. 비교 선택 상자의 잘린 프레임 표기와 업로드 열 너비를 고친 뒤 `Editor.vcxproj`와 `CreatorEditor.vcxproj`를 다시 빌드·링크했다. 최종 배치의 별도 화면 캡처는 하지 않았다.
- Summary는 세로로 쌓인 구간 막대/범례로 커밋 주소 영역(Private/Image/Mapped), GC 힙(비단편/단편화), 계측 가능한 엔진 CPU 텍스처 픽셀(Texture/UI Texture/Sprite Sheet)을 나타낸다. 세 그래프는 별도 외곽선 카드 안에 두고, 범위가 겹치는 OS·CRT·GPU 값은 그 아래 별도 지표 카드에 둔다. Unity의 Native/Graphics/Audio/Unknown이나 관리 객체의 정확한 크기는 현재 수집원이 없어 그래프에 추정치로 넣지 않는다.
- 그래프 재배치 후 Debug `Editor.vcxproj`/`CreatorEditor.vcxproj` 빌드·링크를 통과했고 실제 창의 `Build/Validation/MemoryProfiler/20260923/ui-redesign/summary-final.png`에서 세 그래프와 범례 배치를 확인했다. `summary-assets.png`는 `CreatorRobot.glb` 적재 후 화면이다. 이 자극은 모델 캐시 객체 1개만 보존해 CPU 텍스처 픽셀 그래프가 0 B로 표시되며, 렌더러/관리 런타임의 스냅샷 시점에 값이 없으면 각 그래프는 빈 트랙과 0 B를 명시한다.
- 카드 좌우 바깥 여백과 안쪽 패딩을 추가하고 막대 높이를 22→42 ImGui 단위로 늘렸다. Debug `Editor.vcxproj`/`CreatorEditor.vcxproj`를 다시 빌드·링크한 다음 실제 창의 `Build/Validation/MemoryProfiler/20260923/ui-redesign/summary-spacing.png`에서 막대 두께, 카드 경계, 범례 간격을 확인했다.
- 범례의 색상 사각형은 글자와 다른 높이 기준을 사용하던 `Dummy`/`SameLine` 배치를 없애고, 실제 텍스트 영역의 세로 중앙에 직접 그린다. Debug 두 프로젝트 빌드·링크 후 `Build/Validation/MemoryProfiler/20260923/ui-redesign/summary-alignment.png`에서 Private/Image/Mapped 등 모든 범례 행의 정렬을 확인했다.

## 아직 같지 않은 것

현재의 Engine Objects는 **자산 캐시 목록**이며 Unity의 모든 Native/Managed Objects 목록은 아니다. `VirtualQuery` 지도는 주소 구성이지만 객체별 할당 추적이 아니다. VRAM은 백엔드의 어댑터 표본이며 개별 GPU 자산의 크기·참조 관계가 아니다. 따라서 MP2~MP6 없이 Unity Memory Profiler와 동등한 상세 분석이라고 판정하지 않는다.
