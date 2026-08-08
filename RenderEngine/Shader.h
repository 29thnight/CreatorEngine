#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Core.Minimal.h"

//-----------------------------------------------------------------------------
// 셰이더 자산: 컴파일된 blob (M1, 2026-08-08)
//
// ★ 파생 여섯이 각각 ID3D11VertexShader/PixelShader/... 오브젝트를 만들어
//   들고 있던 것을 걷었다. 그것을 GPU에 거는 코드는 PipelineStateObject::Apply
//   뿐이었고, 그 Apply의 호출자가 0이었다(마지막 경로 IRenderPass가 D4에서 소멸).
//
// ★ blob은 남는다 — 그것이 진짜 자산이고, DX12가 PSO를 구울 때 넣는 입력이
//   정확히 이 바이트다. 즉 DX11 오브젝트는 같은 blob에서 파생된 사본이었고,
//   아무도 안 쓰는 사본이었다.
//
// 파생 클래스 여섯은 타입 태그로 남긴다. ShaderPtr<VertexShader>처럼 타입으로
// 종류를 구분하는 자리가 여럿이라(PipelineStateObject의 멤버, ShaderSystem의
// 컨테이너) 하나로 합치면 그쪽이 전부 흔들린다 — M2에서 다룬다.
//-----------------------------------------------------------------------------

class IShader
{
public:
	IShader() : m_name("null") {};
	IShader(const std::string& name, const ComPtr<ID3DBlob>& blob)
		: m_name(name), m_blob(blob), m_isCompiled(nullptr != blob) {};
	virtual ~IShader() = default;

	std::string m_name;

	inline void* GetBufferPointer()
	{
		return m_blob->GetBufferPointer();
	}

	inline size_t GetBufferSize()
	{
		return m_blob->GetBufferSize();
	}

	/// blob이 손에 들어왔음을 표시한다.
	///
	/// ★ 예전에는 여기서 DX11 셰이더 오브젝트를 만들었고, 그래서 이 함수가
	///   디바이스를 필요로 했다. 지금은 blob의 유무가 곧 준비 상태다.
	void Compile()
	{
		m_isCompiled = (nullptr != m_blob);
	}

	/// 핫리로드: blob을 갈아 끼운다.
	void SwapAndReCompile(const ComPtr<ID3DBlob>& blob)
	{
		m_blob = blob;
		Compile();
	}

	void Reset()
	{
		m_blob.Reset();
		m_isCompiled = false;
	}

	bool IsCompiled() const
	{
		return m_isCompiled;
	}

	/// 쓸 수 있는 바이트코드가 있는가.
	bool HasBlob() const
	{
		return nullptr != m_blob;
	}

protected:
	ComPtr<ID3DBlob> m_blob;
	bool m_isCompiled{ false };
};

template<typename T>
concept ShaderType = std::derived_from<T, IShader>;

// ── 타입 태그 여섯 ──
//
// 동작은 전부 IShader에 있다. 파생이 남은 이유는 종류를 타입으로 구분하는
// 자리들 때문이고, 그 자체가 자산의 성질(정점 셰이더인가 픽셀 셰이더인가)이라
// 백엔드가 사라져도 남는 구분이다.

class VertexShader final : public IShader
{
public:
	VertexShader() = default;
	VertexShader(const std::string& name, const ComPtr<ID3DBlob>& blob) : IShader(name, blob) {};
};

class PixelShader final : public IShader
{
public:
	PixelShader() = default;
	PixelShader(const std::string& name, const ComPtr<ID3DBlob>& blob) : IShader(name, blob) {};
};

class ComputeShader final : public IShader
{
public:
	ComputeShader() = default;
	ComputeShader(const std::string& name, const ComPtr<ID3DBlob>& blob) : IShader(name, blob) {};
};

class GeometryShader final : public IShader
{
public:
	GeometryShader() = default;
	GeometryShader(const std::string& name, const ComPtr<ID3DBlob>& blob) : IShader(name, blob) {};
};

class HullShader final : public IShader
{
public:
	HullShader() = default;
	HullShader(const std::string& name, const ComPtr<ID3DBlob>& blob) : IShader(name, blob) {};
};

class DomainShader final : public IShader
{
public:
	DomainShader() = default;
	DomainShader(const std::string& name, const ComPtr<ID3DBlob>& blob) : IShader(name, blob) {};
};

template<ShaderType T>
class ShaderPtr
{
public:
	T* operator->() const
	{
		return m_shader;
	}

	T& operator*() const
	{
		return *m_shader;
	}

	T* Get() const
	{
		return m_shader;
	}

	bool IsInvalidated() const
	{
		return nullptr == m_shader || false == m_interface->IsCompiled();
	}

	operator bool() const
	{
		return nullptr != m_shader;
	}

	// C++20이 == 하나에서 !=를 만들어 준다. 둘 다 적으면 재작성 후보가
	// 오버로드 해결에서 빠져 모호해진다(C7692).
	bool operator==(std::nullptr_t) const
	{
		return nullptr == m_shader;
	}

	ShaderPtr() = default;
	ShaderPtr(T* shader) : m_shader(shader), m_shader_identifier(m_shader->m_name), m_interface(shader)
	{
		if ("null" == m_shader->m_name)
		{
			m_shader = nullptr;
			m_interface = nullptr;
		}
	}

	ShaderPtr& operator=(T* shader)
	{
		m_shader = shader;
		m_shader_identifier = shader->m_name;
		m_interface = shader;
		return *this;
	}

	std::string	m_shader_identifier{};
	T*			m_shader{ nullptr };
	IShader*	m_interface{ nullptr };
};
#endif // !DYNAMICCPP_EXPORTS
