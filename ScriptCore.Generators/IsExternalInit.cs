namespace System.Runtime.CompilerServices;

/// <summary>
/// netstandard2.0에는 이 타입이 없어서 record의 init 접근자를 쓸 수 없다.
/// 소스 제너레이터는 Roslyn 안에서 돌기 때문에 타겟을 올릴 수 없으므로 폴리필로 채운다.
/// (컴파일러가 존재 여부만 보므로 내용은 비어 있어도 된다)
/// </summary>
internal static class IsExternalInit;
