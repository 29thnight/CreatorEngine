# mikktspace

탄젠트 공간 생성의 **업계 정본**. Blender·Unity·UE·xNormal 이 같은 코드를 쓰기
때문에, DCC 에서 베이크한 노멀맵이 엔진에서 그대로 맞으려면 이것이어야 한다.

```
판본  : 1.0 (mmikk/MikkTSpace, master)
출처  : https://github.com/mmikk/MikkTSpace
파일  : mikktspace.h (8,209 B) · mikktspace.c (57,439 B)
저자  : Morten S. Mikkelsen
라이선스: zlib 계열 (상용 사용 허용, 출처 왜곡 금지, 고지 유지)
```

**원본 그대로다 — 한 줄도 고치지 않았다.** 라이선스 3항이 고지 제거·변경을
금지하고 2항이 개변 시 명시를 요구한다. 헤더 자신도 "stand-alone 으로 유지하는
것이 중요하다"고 못박고 있다. 갱신은 두 파일을 덮어쓰고 위 판본 줄만 고친다.

## 왜 vcpkg 가 아닌가

임포트 계층 의존을 한곳에 모으는 정책이다. 짝이 되는 ufbx 가 vcpkg 에 없어
어차피 벤더링해야 하고, fastgltf 도 같은 이유로 여기 있다
([../README.md](../README.md) 표 참조).

## 통합 시 반드시 지킬 것

헤더가 대문자로 경고하는 규약이 하나 있다.

> Note that the results are returned unindexed. (...) averaging/overwriting
> tangent spaces by using an already existing index list WILL produce
> INCORRECT results. DO NOT! use an already existing index list.

mikktspace 는 **면-정점(코너) 단위**로 탄젠트를 돌려준다. 같은 정점이라도 면에
따라 다른 값이 나올 수 있고(UV 이음매·거울 대칭), 이것을 기존 인덱스로 덮어쓰면
이음매에서 탄젠트가 뭉개진다. 그래서 소비 측은 **코너별로 받아 두었다가 재용접**
해 새 인덱스 리스트를 만들어야 한다.

이 저장소의 통합은 `Engine/RenderEngine/Experiment/Import/TangentGeneration.cpp`
한 곳이며, 위 재용접을 수행한다. 합성 검사는 `experiment.tangent`.

## 빌드 편입

C 소스라 RenderEngine 의 C++ 유니티 빌드·PCH 에 섞이면 안 된다. `.vcxproj` 의
per-file 설정 3종이 필수다:

| 설정 | 왜 |
|---|---|
| `CompileAs=CompileAsC` | C99 소스다 |
| `PrecompiledHeader=NotUsing` | 프로젝트가 PCH 를 강제한다 |
| `IncludeInUnityFile=false` | 유니티 빌드가 매크로·정적 심볼을 충돌시킨다 |

경고도 끈다(`TurnOffAllWarnings`) — 남의 코드를 고치지 않기로 했으므로 경고를
없앨 방법이 편집밖에 없다.
