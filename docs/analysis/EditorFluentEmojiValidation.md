# Fluent Emoji 이미지 자원과 preset 연결

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 현재 자원

엔티티와 파일/폴더 그림은 Microsoft Fluent Emoji의 3D transparent PNG를 사용한다. 배포 출처는 [provenance manifest](../../Resources/Editor/Icons/FluentEmoji.provenance.json)에 source commit, 원본 path/URL, 파일별 SHA-256와 MIT license hash로 고정되어 있다. manifest의 modifications는 원본 PNG bytes를 변경하지 않았다고 기록한다. 이 문서 변경에서는 이미지·manifest·license를 수정하지 않았다.

EntityIconPresets가 아홉 semantic preset ID를 Entity.png, EntityManager.png 등 실제 파일에 매핑한다. EditorAssetPresentation이 읽고 Inspector의 40 logical px 버튼과 Hierarchy 행에서 같은 자원을 사용한다. 이미지가 없으면 표면의 fallback 경로와 기본 preset을 확인해야 하며 unknown ID가 파일 경로로 해석되지는 않는다.

## 재현 도구와 역할 분리

`Tools/icons/Fetch-FluentEmoji.ps1`은 manifest에 기록된 정확한 URL에서 임시 파일로 내려받고 hash가 맞을 때만 destination을 교체한다. 기존 파일 hash가 같으면 건너뛰고 license도 같은 방식으로 복구한다. 현재 브랜치 최신 그림을 임의로 받는 도구가 아니다. 새 그림을 선택할 때는 provenance와 license를 함께 검토해야 한다.

toolbar의 단색 기능 아이콘은 Material Symbols 폰트 체계이며 Fluent Emoji PNG와 별개다. 텍스트 속 emoji fallback도 system font 경로로 처리될 수 있어 세 자원 체계를 혼동하지 않는다. [아이콘 선택 정본](EditorIconSelectionStudy.md)이 semantic role과 글꼴 생성 경계를 설명한다.

## 검증 범위

원본 hash와 manifest의 일치는 자원 신뢰성이고, DPI별 표시 크기·투명 배경·색상·행 정렬은 실제 rendering 수용이다. 초기 @icons→Fluent 전환의 Artifacts 화면은 당시 비교 자료로 남고 현재 화면을 다시 캡처한 결과가 아니다. 파일 누락 fallback, packaged editor resource 경로, 아홉 preset 저장/재로드를 별도로 검증한다.

## 구현 근거

- [Resources/Editor/Icons/FluentEmoji.provenance.json](../../Resources/Editor/Icons/FluentEmoji.provenance.json)
- [Tools/icons/Fetch-FluentEmoji.ps1](../../Tools/icons/Fetch-FluentEmoji.ps1)
- [Editor/EngineEntry/EditorEntityIcons.h](../../Editor/EngineEntry/EditorEntityIcons.h)
- [Editor/EngineEntry/EditorAssetPresentation.cpp](../../Editor/EngineEntry/EditorAssetPresentation.cpp)
- [Editor/EngineGUIWindow/InspectorWindow.cpp](../../Editor/EngineGUIWindow/InspectorWindow.cpp)
