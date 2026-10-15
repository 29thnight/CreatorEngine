# MAT-7 제품 편집·reload·Player 복구의 현재 판정

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 편집의 세 가지 성공

현재 Editor는 draft compile, accepted asset 게시, disk 저장을 분리한다. Apply는 준비된 후보를 expected accepted generation과 대조해 게시한다. Save는 현재 적용된 상태와 디스크 충돌 여부를 확인한 뒤 material/graph를 저장한다. Assign은 저장된 clean asset reference를 Scene object에 넣고 Undo와 Scene 저장 경계를 따른다.

자동 저장도 있다. gesture가 끝나고 draft가 안정된 뒤 compile/preparation이 현재 revision과 일치하면 Apply 후 Save를 한 revision에 한 번 시도한다. 과거의 “버튼만 누를 때 저장” 설명을 현재 정책으로 사용하지 않는다. [Material Node Editor](../design/MaterialNodeEditor.md)에 정확한 상태 흐름을 모았다.

## 명시적 Reload는 엄격한 원본 읽기

일반 archive load의 `.bak` 복구와 사용자가 누른 Editor Reload는 다른 요청이다. Editor는 primary bytes를 엄격히 읽고 material identity와 graph reference를 확인한다. 손상된 원본을 정상 backup으로 조용히 대체해 “reload 성공”이라 표시하지 않는다. dirty draft는 확인을 거치고 성공한 reload는 draft만 교체하며 즉시 renderer에 publish하지 않는다.

compile/disk/texture/identity 오류는 accepted runtime과 정상 디스크를 보존한다. stale async 결과는 document ID/revision이 다르면 버린다. HTTP 편집 명령도 같은 ID/revision guard를 따라야 한다.

## source-free Player 복구

현재 Graph cooked contract는 typed binary validation을 거쳐 accepted generation을 복원한다. Player가 authoring text parser·Slang·source reflection에 의존하도록 되돌리지 않는다. 초기에 고친 LXMC v4 parser-free 경계는 현재 version 12에서도 유지해야 할 요구이며, v4 자체가 현행 artifact version은 아니다.

package와 Editor warm cache의 성공은 다른 증거다. 실제 AssetCooker는 backend target 집합과 dependency를 포함한 package를 만들어야 하고 Player는 source/compiler 없이 그 결과를 소비해야 한다.

## 재현 환경과 남은 UI gate

`sync-material-editor-scale.ps1`은 현재 작업 프로젝트의 배율을 fixture로 복사하기 위한 도구다. fixture 실행이 사용자의 원래 프로젝트 UI 배율을 바꾸면 안 된다. 환경 복구와 임시 프로젝트 격리를 검증 범위에 포함한다.

이전 제품 회귀의 check 수·build log는 당시 상태이며 이번에는 재실행하지 않았다. 실제 마우스 gesture, 문서/Scene 전환 중 compile, 닫기·재열기·shutdown, 외부 disk 변경, undo/redo와 자동 저장의 충돌, 실패 후 같은 revision 재시도는 여전히 제품 수준 증거가 필요하다.

## 구현 근거

- [`Editor/EngineGUIWindow/MaterialGraphWindow.cpp`](../../Editor/EngineGUIWindow/MaterialGraphWindow.cpp)
- [`Editor/EngineEntry/Commands/MaterialGraphCommands.cpp`](../../Editor/EngineEntry/Commands/MaterialGraphCommands.cpp)
- [`Lattice/Material/LXMaterialGraph.cpp`](../../Lattice/Material/LXMaterialGraph.cpp)
- [`Engine/RenderEngine/MaterialGraphProduct.cpp`](../../Engine/RenderEngine/MaterialGraphProduct.cpp)
- [`Engine/RenderEngine/MaterialGraphRuntime.cpp`](../../Engine/RenderEngine/MaterialGraphRuntime.cpp)
- [`Tools/regression/sync-material-editor-scale.ps1`](../../Tools/regression/sync-material-editor-scale.ps1)
