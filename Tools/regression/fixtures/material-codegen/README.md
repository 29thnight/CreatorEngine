# MAT-6 deterministic source fixtures

`constant`, `image-linear-repeat-srgb`, `nested-shared`, `special-volume`의
`.shadergraph`와 expected `.slang`을 함께 고정했다. 원래 graph ID와 node/socket ID를 보존한다.
`.slang`은 읽기 전용 생성 기준선이며 artist source나 Blender renderer golden이 아니다.

`Tools/regression/verify-material-codegen.ps1`는 source의 UTF-8/LF bytes와 graph 재개방 후
semantic identity를 대조한다. 일반 검증은 이 파일들을 덮어쓰지 않는다.
원래 fixture/생성 의미가 바뀌는 경우에만 변경 내용을 검토하고 `-WriteGolden`으로 다시 고정한다.

지원·색 공간·그룹·진단·제품 경계는
[MaterialSlangCodegen.md](../../../../docs/design/MaterialSlangCodegen.md)를 따른다.
