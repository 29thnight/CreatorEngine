namespace CreatorEngine;

/// <summary>
/// 네이티브 <c>Animator</c>의 스크립트 쪽 얼굴.
///
/// 실측에서 호출 122회 중 94회가 <c>SetParameter</c> 하나에 몰려 있어, 파라미터 접근을
/// 가장 먼저 열었다. 엔진은 타입 하나짜리 템플릿(<c>SetParameter&lt;T&gt;</c>)이지만
/// 여기서는 Unity 관례대로 타입별 메서드로 나눈다 — 경계를 넘는 함수는 평면 C ABI라
/// 어차피 타입마다 하나씩 필요하고, 호출부에서 의도가 더 분명해진다.
///
/// 파라미터 이름은 엔진이 선형 탐색으로 찾는다. 개수가 십여 개 수준이라 문제되지 않고,
/// 인덱스를 캐시하려면 컨트롤러 교체·핫리로드마다 무효화를 관리해야 해서 지금은 두지 않는다.
/// </summary>
public sealed class Animator : NativeComponent
{
    // ── 파라미터 쓰기 ──

    public void SetBool(string name, bool value) => Native.AnimatorSetBool(OwnerHandle, name, value);
    public void SetFloat(string name, float value) => Native.AnimatorSetFloat(OwnerHandle, name, value);
    public void SetInt(string name, int value) => Native.AnimatorSetInt(OwnerHandle, name, value);

    /// <summary>트리거를 세운다. 전이가 일어나면 엔진이 되돌린다.</summary>
    public void SetTrigger(string name) => Native.AnimatorSetTrigger(OwnerHandle, name);

    public void ResetTrigger(string name) => Native.AnimatorResetTrigger(OwnerHandle, name);

    // ── 파라미터 읽기 ──
    //
    // 없는 이름을 물으면 기본값(false/0)이 나온다. 오타를 조용히 삼키므로,
    // 값이 이상하면 HasParameter로 먼저 확인할 것.

    public bool GetBool(string name) => Native.AnimatorGetBool(OwnerHandle, name);
    public float GetFloat(string name) => Native.AnimatorGetFloat(OwnerHandle, name);
    public int GetInt(string name) => Native.AnimatorGetInt(OwnerHandle, name);

    public bool HasParameter(string name) => Native.AnimatorHasParameter(OwnerHandle, name);

    // ── 레이어·재생 ──

    /// <summary>레이어를 켜고 끈다. 상체/하체를 나눠 쓰는 형태에서 주로 쓰인다(실측 21회).</summary>
    public void SetUseLayer(int layerIndex, bool useLayer)
        => Native.AnimatorSetUseLayer(OwnerHandle, layerIndex, useLayer);

    /// <summary>주어진 시간 동안 재생을 멈춘다(피격 경직 등).</summary>
    public void StopAnimation(float duration) => Native.AnimatorStopAnimation(OwnerHandle, duration);
}
