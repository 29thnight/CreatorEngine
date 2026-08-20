namespace CreatorEngine;

/// <summary>
/// 네이티브 <c>MeshRenderer</c>의 스크립트 쪽 얼굴.
///
/// 재질(Material)에는 별도 래퍼를 두지 않고 여기서 바로 만진다. 재질은 Entity가
/// 아니라 세대 핸들을 걸 자리가 없고, 스크립트가 하는 일도 "이 오브젝트의 재질을
/// 손본다"가 전부이기 때문이다.
///
/// 실측에서 <c>m_Material</c> 접근이 42회인데 그 중 24회가 셰이더 상수 넣기,
/// 6회가 재질 사본 만들기다.
/// </summary>
public sealed class MeshRenderer : NativeComponent
{
    public string MaterialName => Native.MeshGetMaterialName(OwnerHandle);

    /// <summary>
    /// 재질 사본을 만들어 이 오브젝트에만 붙인다.
    ///
    /// 재질은 기본적으로 여러 오브젝트가 공유한다. 사본을 만들지 않고 색이나 상수를
    /// 바꾸면 같은 재질을 쓰는 것들이 <b>전부 함께 변한다</b>. 개별 연출을 하려면
    /// 먼저 이걸 불러야 한다.
    /// </summary>
    public void InstantiateMaterial(string newName) => Native.MeshInstantiateMaterial(OwnerHandle, newName);

    /// <summary>
    /// 셰이더 상수 버퍼에 float 값을 넣는다.
    /// 버퍼 이름이나 변수 이름이 틀리면 <c>false</c>다 — 엔진이 조용히 실패하므로
    /// 값이 안 먹으면 이 반환값부터 확인할 것.
    /// </summary>
    public bool SetMaterialFloat(string bufferName, string valueName, float value)
        => Native.MeshSetMaterialFloat(OwnerHandle, bufferName, valueName, value);

    /// <summary><see cref="SetMaterialFloat"/>의 int 판.</summary>
    public bool SetMaterialInt(string bufferName, string valueName, int value)
        => Native.MeshSetMaterialInt(OwnerHandle, bufferName, valueName, value);

    /// <summary>
    /// 재질의 베이스 색. 알파를 낮춰 페이드하는 데 주로 쓴다.
    /// 사본을 만들지 않았다면 같은 재질을 쓰는 모든 오브젝트가 함께 바뀐다.
    /// </summary>
    public Color4 BaseColor
    {
        get => Native.MeshGetBaseColor(OwnerHandle);
        set => Native.MeshSetBaseColor(OwnerHandle, value);
    }
}
