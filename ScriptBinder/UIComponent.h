#pragma once
#include "Component.h"
#include "IRenderable.h"
#include "Canvas.h"
#include "Navigation.h"
#include "UIComponent.generated.h"

extern float MaxOreder;

//아직안씀 
enum class UItype
{
	Image,
	Text,
	None,
};


class UIComponent : public Component
{
public:
   ReflectUIComponent
    [[Serializable(Inheritance:Component)]]
	GENERATED_BODY(UIComponent)

   void SetCanvas(Canvas* canvas);
	Canvas* GetOwnerCanvas() { return ownerCanvas; }
	void SetOrder(int index) { _layerorder = index; }
	int GetLayerOrder() const { return _layerorder; }
	void SetNavi(Direction dir, const std::shared_ptr<GameObject>& otherUI);
	void DeserializeNavi();
	GameObject* GetNextNavi(Direction dir);
	bool IsNavigationThis();

	void DeserializeShader();

	Mathf::Vector3 pos{ 960, 540, 0 };
	Mathf::Vector2 scale{ 1, 1 };
	UItype type = UItype::None;

	static bool CompareLayerOrder(UIComponent* a, UIComponent* b)
	{
		if (a->_layerorder != b->_layerorder)
			return a->_layerorder < b->_layerorder;
		auto aCanvas = a->GetOwnerCanvas();
		auto bCanvas = b->GetOwnerCanvas();
		int aOrder = aCanvas ? aCanvas->GetCanvasOrder() : 0;
		int bOrder = bCanvas ? bCanvas->GetCanvasOrder() : 0;
		return aOrder < bOrder;
	}

	void SetCustomPixelShader(std::string_view shaderPath);
	std::string GetCustomPixelShader() const { return m_customPixelShaderPath; }
	void ClearCustomPixelShader() 
	{ 
		m_customPixelShaderPath.clear(); 
		m_customPixelCPUBuffer.clear();
		m_variables.clear();
	}

	const std::vector<std::byte>& GetCustomPixelCPUBuffer() const { return m_customPixelCPUBuffer; }

	// Variable accessors
	std::optional<float> GetFloat(std::string_view name) const;
	void SetFloat(std::string_view name, float value);
	std::optional<float2> GetFloat2(std::string_view name) const;
	void SetFloat2(std::string_view name, const float2& value);
	std::optional<float3> GetFloat3(std::string_view name) const;
	void SetFloat3(std::string_view name, const float3& value);
	std::optional<float4> GetFloat4(std::string_view name) const;
	void SetFloat4(std::string_view name, const float4& value);

	std::optional<int> GetInt(std::string_view name) const;
	void SetInt(std::string_view name, int value);
	std::optional<int2> GetInt2(std::string_view name) const;
	void SetInt2(std::string_view name, const int2& value);
	std::optional<int3> GetInt3(std::string_view name) const;
	void SetInt3(std::string_view name, const int3& value);
	std::optional<int4> GetInt4(std::string_view name) const;
	void SetInt4(std::string_view name, const int4& value);

    [[Property]]
	int _layerorder{};
	[[Property]]
	std::string m_ownerCanvasName{};
	[[Property]]
	std::vector<Navigation> navigations{};

private:
	std::array<std::weak_ptr<GameObject>, NavDirectionCount> navigation;
	Canvas* ownerCanvas = nullptr;

protected:
	[[Property]]
	std::string				m_customPixelShaderPath{};
	std::vector<std::byte>	m_customPixelCPUBuffer{};

	struct VarInfo { UINT offset; UINT size; };
	std::unordered_map<std::string, VarInfo> m_variables;
};

