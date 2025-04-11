#pragma once
#include "DeviceState.h"

class IShader
{
public:
	IShader() : m_name("null") {};
	IShader(const std::string_view& name, const ComPtr<ID3DBlob>& blob) : m_name(name), m_blob(blob), m_isCompiled(false) {};
	virtual ~IShader() = default;

	virtual void Compile() = 0;
	virtual void Reset() = 0;
	std::string m_name;

	inline void* GetBufferPointer()
	{
		return m_blob->GetBufferPointer();
	}

	inline size_t GetBufferSize()
	{
		return m_blob->GetBufferSize();
	}

	void SwapAndReCompile(const ComPtr<ID3DBlob>& blob)
	{

		if (m_isCompiled)
		{
			Reset();
			m_blob.Reset();
			m_isCompiled = false;
		}
		m_blob = blob;
		Compile();
	}

protected:
	bool m_isCompiled{ false };
	ComPtr<ID3DBlob> m_blob;
};

template<typename T>
concept ShaderType = std::derived_from<T, IShader>;

template<ShaderType T>
class ShaderPtr
{
public:
	T* operator->()
	{
		return m_shader;
	}

	T& operator*()
	{
		return *m_shader;
	}

	operator T* ()
	{
		return m_shader;
	}

	bool operator==(std::nullptr_t)
	{
		return m_shader == nullptr;
	}

	ShaderPtr() = default;
	ShaderPtr(T* shader) : m_shader(shader), m_shader_identifier(m_shader->m_name) 
	{
		if ("null" == m_shader->m_name)
		{
			throw std::runtime_error("Shader name is null");
		}
	};
	~ShaderPtr() = default;
	ShaderPtr& operator=(T* shader)
	{
		m_shader = shader;
		m_shader_identifier = shader->m_name;
		
		return *this;
	}

	std::string m_shader_identifier{};
	T* m_shader{ nullptr };
};

class VertexShader final : public IShader
{
public:
	VertexShader() = default;
	VertexShader(const std::string_view& name, const ComPtr<ID3DBlob>& blob) : IShader(name, blob) {};
	~VertexShader() = default;

	void Compile() override
	{
		DirectX11::ThrowIfFailed(
			DeviceState::g_pDevice->CreateVertexShader(
				m_blob->GetBufferPointer(), 
				m_blob->GetBufferSize(), 
				nullptr, 
				&m_vertexShader
			)
		);
		m_isCompiled = true;
	}

	ID3D11VertexShader* GetShader() const
	{
		return m_vertexShader.Get();
	}

	void Reset() override
	{
		m_vertexShader.Reset();
	}

private:
	ComPtr<ID3D11VertexShader> m_vertexShader;
};

class PixelShader final : public IShader
{
public:
	PixelShader() = default;
	PixelShader(const std::string_view& name, const ComPtr<ID3DBlob>& blob) : IShader(name, blob) {};
	~PixelShader() = default;
	void Compile() override
	{
		DirectX11::ThrowIfFailed(
			DeviceState::g_pDevice->CreatePixelShader(
				m_blob->GetBufferPointer(),
				m_blob->GetBufferSize(),
				nullptr,
				&m_pixelShader
			)
		);
		m_isCompiled = true;
	}
	ID3D11PixelShader* GetShader() const
	{
		return m_pixelShader.Get();
	}
	void Reset() override
	{
		m_pixelShader.Reset();
	}

private:
	ComPtr<ID3D11PixelShader> m_pixelShader;
};

class ComputeShader final : public IShader
{
public:
	ComputeShader() = default;
	ComputeShader(const std::string_view& name, const ComPtr<ID3DBlob>& blob) : IShader(name, blob) {};
	~ComputeShader() = default;
	void Compile() override
	{
		DirectX11::ThrowIfFailed(
			DeviceState::g_pDevice->CreateComputeShader(
				m_blob->GetBufferPointer(),
				m_blob->GetBufferSize(),
				nullptr,
				&m_computeShader
			)
		);
		m_isCompiled = true;
	}
	ID3D11ComputeShader* GetShader() const
	{
		return m_computeShader.Get();
	}
	void Reset() override
	{
		m_computeShader.Reset();
	}
private:
	ComPtr<ID3D11ComputeShader> m_computeShader;
};

class GeometryShader final : public IShader
{
public:
	GeometryShader() = default;
	GeometryShader(const std::string_view& name, const ComPtr<ID3DBlob>& blob) : IShader(name, blob) {};
	~GeometryShader() = default;
	void Compile() override
	{
		DirectX11::ThrowIfFailed(
			DeviceState::g_pDevice->CreateGeometryShader(
				m_blob->GetBufferPointer(),
				m_blob->GetBufferSize(),
				nullptr,
				&m_geometryShader
			)
		);
		m_isCompiled = true;
	}
	ID3D11GeometryShader* GetShader() const
	{
		return m_geometryShader.Get();
	}
	void Reset() override
	{
		m_geometryShader.Reset();
	}
private:
	ComPtr<ID3D11GeometryShader> m_geometryShader;
};

class HullShader final : public IShader
{
public:
	HullShader() = default;
	HullShader(const std::string_view& name, const ComPtr<ID3DBlob>& blob) : IShader(name, blob) {};
	~HullShader() = default;
	void Compile() override
	{
		DirectX11::ThrowIfFailed(
			DeviceState::g_pDevice->CreateHullShader(
				m_blob->GetBufferPointer(),
				m_blob->GetBufferSize(),
				nullptr,
				&m_hullShader
			)
		);
		m_isCompiled = true;
	}
	ID3D11HullShader* GetShader() const
	{
		return m_hullShader.Get();
	}
	void Reset() override
	{
		m_hullShader.Reset();
	}
private:
	ComPtr<ID3D11HullShader> m_hullShader;
};

class DomainShader final : public IShader
{
public:
	DomainShader() = default;
	DomainShader(const std::string_view& name, const ComPtr<ID3DBlob>& blob) : IShader(name, blob) {};
	~DomainShader() = default;
	void Compile() override
	{
		DirectX11::ThrowIfFailed(
			DeviceState::g_pDevice->CreateDomainShader(
				m_blob->GetBufferPointer(),
				m_blob->GetBufferSize(),
				nullptr,
				&m_domainShader
			)
		);
		m_isCompiled = true;
	}
	ID3D11DomainShader* GetShader() const
	{
		return m_domainShader.Get();
	}
	void Reset() override
	{
		m_domainShader.Reset();
	}
private:
	ComPtr<ID3D11DomainShader> m_domainShader;
};