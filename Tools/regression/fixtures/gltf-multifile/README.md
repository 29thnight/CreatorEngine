# 다중 파일 glTF fixture

`verify-model-multifile-import.ps1` 전용. 손으로 만든 것이고 외부 자산이 아니다 —
라이선스 의무가 없다.

| 파일 | 무엇인가 |
|---|---|
| `MultiFileProbe.gltf` | 쿼드 1개 · 재질 1개. 버퍼와 이미지를 **외부 URI**로 참조한다. |
| `MultiFileProbe.bin` | 인덱스 6 + 위치·법선·UV 각 4정점(140바이트). |
| `Textures/Probe.png` | 4×4 RGBA 체크무늬(84바이트). |

## 왜 저장소가 이 fixture를 직접 소유하나

이 결함의 원래 재현체는 `Dynamic_CPP/Assets/Models/TextureSettingsTest/`
(Khronos glTF-Sample-Assets, `PROVENANCE.md` 참조)다. 그런데 그 폴더는
`.gitignore`의 `/Dynamic_CPP/Assets/Models/*`에 막혀 **추적 밖**이다 — 한 기계의
디스크에만 있다는 뜻이고, 그것만 쓰는 게이트는 clean checkout에서 조용히
비어 버린다(`Tools/regression/fixtures/imgui-ini`가 같은 이유로 생겼다).

## `Textures/` 하위 폴더가 핵심이다

깨진 임포트는 `destinationDirectory / source.filename()`로 **파일 하나만** 복사해
폴더 구조를 평탄화했다. 사이드카를 평면으로 복사하는 어중간한 수정은 `.bin`은
우연히 맞히지만 하위 폴더 이미지는 반드시 깨뜨린다. 이 fixture가 그 둘을 가른다.

## 적대적 입력은 여기 두지 않는다

`..` 탈출 · `http://` 원격 · 없는 사이드카는 게이트가 **실행 시각에** 이 fixture의
`uri`를 고쳐 심어 만든다(`New-StagedFixture`). 저장소에 깨진 자산을 체크인하지
않으려는 것이고, 변형이 하나뿐인 원본에서 파생되므로 대조가 분명해진다.

## 다시 만들려면

`MultiFileProbe.bin`과 `Textures/Probe.png`는 생성물이다. 손으로 고치지 말고
게이트 도입 커밋(`Tools/regression/verify-model-multifile-import.ps1` 추가분)의
생성 절차를 따라 다시 만든다 — `.bin`의 `bufferViews` 오프셋(0/12/60/108)과
`.gltf`의 기재가 맞아야 한다.
