# CreatorRobot — Blender 준비본

엔진 임포트 전 단계의 에셋이다. CreatorEditor는 실행하지 않았으며 엔진의 Assets 폴더에 등록하지 않았다.

## 파일

- `CreatorRobot.blend`: 편집 가능한 리그와 기본 8개 Action. 텍스처 내장.
- `CreatorRobot.glb`: 위 메시·리그·8개 클립·텍스처를 포함한 단일 파일.
- `RigOnly/CreatorRobot_Rig.blend`, `.glb`: 같은 메시·리그의 T 포즈 원본. SU_Mythic 클립 없음.
- `Textures/`: 2K BaseColor / OpenGL tangent normal / ORM 텍스처, 재질당 3장.
- `Preview.png`: Blender에서 렌더한 기본 8종 비교.
- `MotionReview/`: 각 클립의 시작부터 끝까지 5개 시점 비교 이미지.
- `preparation.json`: 원본 파일 SHA-256, 53개 본 계층·위치, 리타게팅 대응, 클립 정보.
- `validation.json`: 단위·스케일·가중치·클립·GLB 왕복 검사 결과.

## 단위와 방향

- **1 Blender unit = 1 glTF meter = 1 CreatorEngine unit.**
- T 포즈 키는 **1.800m**. `Scale = (1, 1, 1)`에서 이 크기다. 키가 1유닛이라는 뜻이 아니다.
- 발바닥 최하단을 원점 바닥에 맞췄다. Blender는 Z-up / +Y-forward, GLB는 Y-up / -Z-forward.
- 엔진 GltfImporter가 Z를 반전하므로 CreatorEngine에서는 Y-up / +Z-forward가 된다.
- 메시와 Armature의 Object Transform은 위치 0, 회전 0, 배율 1. 원본 FBX의 0.01 부모 배율은 정점에 적용했다.
- 뼈대의 애니메이션에 확대·축소 키를 사용하지 않는다. 관절의 회전과 골반 위치만 움직인다.
- 엔진에서 0.01 / 0.025 같은 보정 배율을 다시 적용하지 않는다.
- 근거: `Engine/RenderEngine/Experiment/Import/GltfImporter.cpp`의 `originalUnitMeters = 1.0`, `ToEngine`, `ToEngineScale`.

## 모델과 리그

- 원본: 435,616 triangles / 117 mesh objects.
- 준비본: 79,584 triangles / 43,345 Blender vertices / 1 skinned mesh / 4 material slots.
- `CreatorHumanoid 1`: root, hips, spine, chest, upper_chest, neck, head, 좌우 쇄골·팔·손·손가락·다리·발·발가락, 총 53본.
- 모델의 기계 부품은 관절에 단단하게 붙이고, 긴 몸통 연결부에는 연속 가중치를 적용했다.
- 자동 경량화 결과이므로 고급 캐릭터 제작에 필요한 수작업 리토폴로지·LOD 제작을 대체하지 않는다.
- 입력 FBX가 DirectX normal 파일명을 참조하므로, 제공된 normal의 Green 채널을 반전한 `NormalGL`을 사용한다.
- ORM은 R=Occlusion, G=Roughness, B=Metallic. 원래 네 재질의 UV를 유지했다.

## 기본 클립

| Action | 출처 | 재생 시간 | 재생 방식 |
|---|---|---:|---|
| Idle | Skin_SU_Mythic_Swd_Idle_1 + 맨손 자세 보정 | 8.750000초 | 반복 |
| Walk | Skin_SU_Mythic_Swd_Walk_1 + 맨손 자세 보정 | 0.916667초 | 반복 |
| Run | Skin_SU_Mythic_Swd_Run_1 + 맨손 자세 보정 | 0.833333초 | 반복 |
| Jump | 대상 리그에서 Blender로 작성 | 0.600000초 | 1회 |
| FallLoop | 대상 리그에서 Blender로 작성 | 0.800000초 | 반복 |
| Land | 대상 리그에서 Blender로 작성 | 0.600000초 | 1회 |
| Hit | Skin_SU_Mythic_Swd_Dmg_1 + 대기 복귀 보정 | 0.500000초 | 1회 |
| Death | Skin_SU_Mythic_Death_1 + 대기 시작 보정 | 4.166667초 | 1회 후 마지막 자세 유지 |

30fps를 기준으로 키를 굽되, 루프의 마지막 키가 소수 프레임에 놓여도 유지한다. Blender Action의 첫 키 자체를 0프레임으로 정렬하여 GLB에서도 0초부터 시작하게 한다. 비샘플링 내보내기에서는 시작 정렬 옵션만으로 보정되지 않는 경우가 있어 실제 키 시간을 맞춘다.
모든 클립의 root는 원점에 고정한다. 이동의 골반 높이·발 체공 구간은 대상 체형에 맞춰 보정했다.
GLB 클립 순서는 위 표와 같다. 엔진의 기본 클립 0이 Idle이 되도록 내보낸다. 테스트나 아트 연동은 배열 순서보다 클립 이름을 기준으로 연결하는 것이 안전하다.
Idle / Walk / Run의 상체와 팔은 무기를 들지 않는 자세와 팔 흔들기로 정리했다. 원본 다리 움직임과 재생 시간은 유지한다.
Jump / FallLoop / Land는 대상 리그의 관절과 다리 길이로 직접 작성했다. Jump는 준비·압축·발돋움, FallLoop는 공중 자세, Land는 충격 흡수·대기 복귀다.
점프의 실제 월드 상승·낙하와 수평 이동은 엔진의 캐릭터 컨트롤러가 처리해야 한다. 클립에는 전체 점프 궤적을 굽지 않았다. 공중 자세에는 발을 당기는 최대 6cm의 로컬 높이 차이가 있다.
Jump 시작 / Land 끝 / Hit 시작·끝 / Death 시작은 Idle 첫 자세와 맞췄고, Jump 끝 / FallLoop 양 끝 / Land 시작도 일치시켰다.
Death는 root를 움직이지 않으면서 원본 골반의 로컬 이동을 보존하며 바닥에 쓰러진다. 발 대신 몸통 등이 바닥에 닿는 구간을 기준으로 높이를 보정한다.
SU_Mythic의 옷·머리카락·무기 본과 메시·텍스처는 이 파일에 포함하지 않는다. 손가락은 원본에 대응 클립이 없어 기본 자세를 유지한다.

`loop`, `playback`, `root_motion`, `provenance`를 Blender Action 사용자 속성과 `preparation.json`에 기록했다. GLB 자체에는 엔진 공통의 반복/상태 전이 규격이 없으므로 임포트 후 Animator에서도 설정해야 한다.
권장 연결은 `Idle ↔ Walk ↔ Run`, `Jump → FallLoop → Land → 이동 상태`, `Hit → 이전 이동 상태`, `Death → 마지막 자세 유지`다. 이 단계에서 엔진 상태 머신을 생성하거나 실행한 것은 아니다.

## 아트팀 클립 추가

1. `CreatorRobot.blend`를 열고 `CreatorRobot` Armature를 선택한다.
2. Pose Mode와 Dope Sheet → Action Editor에서 새 Action을 만든다. 기존 클립에 덮어쓰지 않도록 새 이름과 Fake User를 설정한다.
3. 같은 `CreatorHumanoid 1` 리그로 저작하면 그대로 사용할 수 있다. 다른 리그의 클립은 본 대응·기준 포즈·단위를 맞춰 리타게팅하고 결과를 이 리그에 굽는다.
4. 본 이름·부모 관계·rest pose는 유지한다. 오브젝트 배율은 1, 씬 단위는 Metric / Unit Scale 1, FPS 30을 유지한다. Action의 첫 키는 0프레임에 놓는다.
5. 제어용 본이나 IK를 추가한 경우 변형용 본에 결과를 굽는다. 전달할 클립만 Action으로 남긴다.
6. GLB export: Selected Objects(Armature + Body), +Y Up, Normals, Tangents, Skinning, Animation Mode=Actions. **Always Sample Animations는 끄고, Set All glTF Animation Starting at 0은 켠다.** 마지막 소수 프레임이 잘리지 않고 시작 시간이 0초가 되게 한다.
7. 엔진 임포트 전 GLB를 새 Blender 씬에 다시 불러 크기·루프·관절 변형을 확인한다.

현재 엔진이 별도 애니메이션 파일을 기존 모델에 바로 연결하는 기능까지 검증한 것은 아니다. 이 준비본의 추가 클립 경로는 Blender에서 같은 리그에 묶어 GLB로 내보내는 방식이다.

## 출처와 배포 범위

모델·텍스처: **Sci-Fi Humanoid Robot**, **yur1_val**.
원본: https://skfb.ly/p7Y9Z
원본 모델 페이지: https://sketchfab.com/3d-models/sci-fi-humanoid-robot-12ad786ecde246e5854a61fd4f67ed49
라이선스: Creative Commons Attribution 4.0 International — https://creativecommons.org/licenses/by/4.0/

CreatorEngine용 변경: 단위·방향 정규화, 경량화, 메시 통합, 휴머노이드 리깅·가중치, 2K 텍스처와 ORM 구성, normal 방향 변환.
배포 시 제작자·원본·라이선스 링크와 이 변경 내역을 유지한다. 제작자가 CreatorEngine을 후원한다는 의미는 아니다.

**SU_Mythic에서 추출한 애니메이션은 위 모델의 CC BY 라이선스에 포함되지 않는다.** 현재 원본 클립의 재배포 권리가 확인되지 않았으므로 SU_Mythic 유래 5개 클립을 포함한 파일은 로컬 저작·검증용으로 취급한다. 자세 보정이 원본 클립의 권리를 바꾸지는 않는다. 자체 제작 클립으로 교체하거나 별도 권리를 확인한 뒤 배포한다. `RigOnly`에는 이 애니메이션이 없다.

## 재생성

저장소의 `Tools/blender/prepare_starter_robot.py`를 Blender 5.1에서 실행한다.
입력은 원본 로봇 폴더와 로컬 `SU_Mythic.glb`이며, 출력은 엔진 Assets 외부의 이 폴더다.
`Tools/blender/validate_starter_robot.py --asset-dir <이 폴더>`도 Blender의 `--python`으로 실행한다.
전체 준비 과정에서 원본 다운로드와 엔진 코드는 변경하지 않는다.
