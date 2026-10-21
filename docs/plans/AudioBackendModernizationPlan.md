# 오디오 백엔드 현대화 — FMOD Core 은퇴 · miniaudio 내재화 (PHASE 22)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/AudioBackendModernizationPlan.md)

## 남은 작업과 완료 판정

- AU0~AU9 구현과 기존 종료 판정은 활성 계획에서 제거한다. miniaudio/Null backend와 resident/stream 경로를 유지한다. FMOD 채택을 새 선행으로 만들지 않는다.
- 게임 CI 레그의 clean build/cook/package와 오디오 콘텐츠 참조 정리는 BuildPipelinePlan이 소유한다. PHASE23 설치 제품 수용은 배포 계획으로 넘긴다.

## 적용할 구조와 경계

`AudioSource/listener → AudioHost/playback → clip directory → miniaudio backend → completion`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## master 소스 근거

모든 행의 파일·행 번호·Git blob은 고정 master에서 대조했다. 소스 존재·배선 확인과 실행 성공을 구분한다.

- [Engine/SceneRuntime/Audio/AudioHost.h:9](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Audio/AudioHost.h#L9) — `enum class AudioHostMode`
- [Engine/SceneRuntime/Audio/PlaybackService.h:10](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Audio/PlaybackService.h#L10) — `struct PlaybackHandle final`
- [Engine/SceneRuntime/Audio/MiniaudioBackend.h:8](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Audio/MiniaudioBackend.h#L8) — `struct AudioCallbackMetrics final`
- [ScriptCore/Audio.cs:8](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Audio.cs#L8) — `public readonly struct AudioAssetId : IEquatable<AudioAssetId>`

## 검증 경계

이번 정리는 master 소스·문서 전체의 소유권/충돌/링크/HTML 검증이다. 엔진 빌드·GPU 픽셀·SDK/장치·성능·실제 GUI 수용을 새로 실행한 결과가 아니다. 과거 통과는 해당 소스/바이너리 신원에만 유효하다. 남은 제품 수용은 위 요구를 실제 산출물로 닫아야 한다.

<details><summary>이전 절 링크 호환</summary>

이전 절의 완료 이력·구안은 제거했다. 이 링크로 들어온 경우 위의 남은 작업과 단일 소유 계획을 따른다.

<span id="0-결정-요약"></span>
<span id="1-현재-소스-기준선-2026-08-27-재확인"></span>
<span id="10-갱신-규칙"></span>
<span id="11-실제-fmod-사용-범위"></span>
<span id="11-착수-기록-2026-09-16"></span>
<span id="111-au0-의-판정문을-바꾼-이유"></span>
<span id="1110-2026-09-24-au0-실장치-성능-기준선"></span>
<span id="1111-2026-09-24-au0-파형루프손상-입력-matrix"></span>
<span id="1112-2026-09-24-뒤쪽-절단임시-resident-경계"></span>
<span id="1113-2026-09-24-au2-자산-등록-포맷-경계"></span>
<span id="1114-2026-09-24-au2-오디오-source-stamp와-import-설정"></span>
<span id="1115-2026-09-24-au2-cook-입력-봉쇄"></span>
<span id="1116-2026-09-24-au2-오프라인-encoded-stream-검증"></span>
<span id="1117-2026-09-24-au2-오디오-artifact와-cemf-게시"></span>
<span id="1118-2026-09-24-au2-cooked-audio-byte-source"></span>
<span id="1119-2026-09-24-au2-cooked-resident-재생-경로"></span>
<span id="112-배선을-파사드가-아니라-신규-작성으로-간다"></span>
<span id="1120-2026-09-24-au2-pak-mount-읽기-수명"></span>
<span id="113-확정한-이름과-경계"></span>
<span id="114-착지한-것"></span>
<span id="115-운영-제약"></span>
<span id="116-fmodbackend-는-출하-뒤로-미룬다-2026-09-16-결정"></span>
<span id="117-종료-canary-가-초록으로-바뀌었다-2026-09-16"></span>
<span id="118-소비자-전환-경계-au7"></span>
<span id="119-2026-09-24-au0au1-보강"></span>
<span id="12-2026-10-05-소비-설계-확정클라우드-분리-검증제품-이행"></span>
<span id="12-포맷과-asset-경로의-현재-모순"></span>
<span id="121-승인된-범위와-두-소비-경로"></span>
<span id="1210-2026-10-08-선택-씬-package-수용"></span>
<span id="1211-최종-도구스트림-계측-재검증--2026-10-08"></span>
<span id="1212-수정-후-실제-editor-30분-수용--pass"></span>
<span id="1213-phase22-최종-종료--2026-10-08"></span>
<span id="122-소비-계약과-수명"></span>
<span id="123-자산마이그레이션배포"></span>
<span id="124-분리-테스트의-경계"></span>
<span id="125-v1의-명시적-경계"></span>
<span id="126-구현과-acceptance-표시"></span>
<span id="127-이번-구현의-검증-정산"></span>
<span id="128-windows-제품-acceptance-후속--2026-10-07"></span>
<span id="129-승인-예산-및-소비자-종료-정산--2026-10-08"></span>
<span id="13-수명스레드-기준선"></span>
<span id="14-기존-출력-중-parity-정본이-아닌-항목"></span>
<span id="15-현재-상태를-완료로-오해하지-않는-규칙"></span>
<span id="2-제품-포맷-계약"></span>
<span id="2026-10-07-최근-병합-반영"></span>
<span id="21-허용-포맷"></span>
<span id="22-channelsample-rate-규칙"></span>
<span id="23-meta와-cooked-clip"></span>
<span id="3-목표-계층과-소유권"></span>
<span id="31-공개-계약"></span>
<span id="32-runtime-소유권"></span>
<span id="33-command와-thread-경계"></span>
<span id="4-기능별-채택-결정"></span>
<span id="41-bus와-volume"></span>
<span id="42-spatial-blend와-rolloff"></span>
<span id="43-voice-capstealingvirtualization"></span>
<span id="44-stream과-resident"></span>
<span id="45-reverb"></span>
<span id="46-device와-장애-복구"></span>
<span id="5-실행-슬라이스"></span>
<span id="6-의존-관계와-배정"></span>
<span id="7-검증-matrix"></span>
<span id="8-최종-완료-기준"></span>
<span id="9-위험과-기각한-대안"></span>
<span id="au0--기준선-유효성-분류합성-fixture실패-canary-p0-3일"></span>
<span id="au1--backend-neutral-계약모듈listenerhost-수명-p0-5일"></span>
<span id="au2--audioclip-importermetacatalogcookvfs-계약-p0-7일"></span>
<span id="au3--miniaudiobackend-미배선-구현-p0-4일"></span>
<span id="au4--core-playbackbusstream-parity-p0-5일"></span>
<span id="au5--listener3dspatial-blendvoice-allocatorvirtualization-p0-6일"></span>
<span id="au6--named-reverb-bussendcapture-검증-p1-3일"></span>
<span id="au7--soundcomponenteditormanaged-소비자-배선-p0-5일"></span>
<span id="au8--fmod-은퇴buildpackageci-폐쇄-p0-3일"></span>
<span id="au9--성능device-장애soak최종-정산-p0-4일"></span>
<span id="sec-0-결정-요약"></span>
<span id="sec-1-현재-소스-기준선-2026-08-27-재확인"></span>
<span id="sec-10-갱신-규칙"></span>
<span id="sec-11-착수-기록-2026-09-16"></span>
<span id="sec-12-2026-10-05-소비-설계-확정클라우드-분리-검증제품-이행"></span>
<span id="sec-2-제품-포맷-계약"></span>
<span id="sec-2026-10-07-최근-병합-반영"></span>
<span id="sec-3-목표-계층과-소유권"></span>
<span id="sec-4-기능별-채택-결정"></span>
<span id="sec-5-실행-슬라이스"></span>
<span id="sec-6-의존-관계와-배정"></span>
<span id="sec-7-검증-matrix"></span>
<span id="sec-8-최종-완료-기준"></span>
<span id="sec-9-위험과-기각한-대안"></span>

</details>
