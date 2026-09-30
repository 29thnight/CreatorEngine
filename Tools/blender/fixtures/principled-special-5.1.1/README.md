# MAT-5 Special 숫자 기준선

- `volume-defaults.json`: Blender 5.1.1 `b70da489d7f4`의 Volume Principled 입력 subset 6개.
  `Tools/blender/export-lx-volume-defaults.py`를 Blender background에서 실행해 exact 재생성한다.
- Surface 입력 6개의 기본값은 `Tools/LatticeExample/fixtures/blender-5.1.1-shader-nodes.json`을 사용한다.
- `numeric-golden.csv`: CreatorEngine 독립 CPU double evaluator의 `0x3FFF`, 35사례×6시선×21필드,
  4,410행 기준선이다. **Blender rendered golden이 아니다.**
- `manifest.json`: LF로 정규화한 UTF-8 SHA-256, case/view/field 범위와 evaluator 경로.
  GPU와 CPU가 함께 바뀌어 이전 baseline을 놓치지 않도록 회귀 wrapper에서 pinned hash와 17,640성분을 대조한다.

## 재현

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' --background --factory-startup `
    --python Tools/blender/export-lx-volume-defaults.py -- Build/Obj/PrincipledSpecialProbe/volume-defaults.json
& Tools/regression/verify-principled-special.ps1
```

검증 실행은 tracked golden을 덮어쓰지 않는다. `Build/Obj/PrincipledSpecialProbe/`에
GPU readback, 새 CPU 결과, backend/stage별 compile byte 수와 log를 기록한다.
기준선 변경은 [Special 의미 계약](../../../../docs/design/PrincipledSpecialSemantics.md)과
근사 수용 판정을 갱신한 뒤 별도로 검토한다.

이 fixture에 Cycles Random Walk, blackbody/temperature/attribute, heterogeneous/multiple volume scattering,
닫힌 solid의 경로 추적이나 화면 기반 transport 구현이 포함됐다고 해석하지 않는다.
