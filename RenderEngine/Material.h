#pragma once
#include "MaterialInfomation.h"
#include "MaterialFlowInformation.h"
// 직접 포함해야 한다. Texture.h를 거쳐 들어오길 기대하면 안 되는데,
// Texture.h는 본문 전체가 #ifndef DYNAMICCPP_EXPORTS로 막혀 있어
// 스크립트 DLL 빌드에서는 Diagnostics가 선언되지 않는다.
#include "EngineResourceCensus.h"
#include "Texture.h"
#include "ShaderPSO.h"
#include "Material.generated.h"
#include <unordered_map>
#include <vector>
#include <cstdint>

class ID3D11DeviceContext;
enum class MaterialRenderingMode
{
	Opaque,
	Transparent,
};
AUTO_REGISTER_ENUM(MaterialRenderingMode);

struct ID3D11DeviceContext;
class Material : private Diagnostics::CountedResource<Diagnostics::EngineResource::Material>
{
public:
   ReflectMaterial
    [[Serializable]]
	Material();
	Material(const Material& material);
	Material(Material&& material) noexcept;
	~Material();

	bool operator==(const Material& other) const
	{
		return m_materialGuid == other.m_materialGuid;
	}

	// 머티리얼 클론은 반드시 소유권과 함께 받는다.
	//
	// 예전에는 원시 포인터를 돌려주는 Instantiate()가 있었는데, 클론의 유일한
	// shared_ptr이 DataSystem 캐시에만 있어서 캐시가 정리되면 사용 중인 클론이
	// 그대로 파괴됐다(12.2 보충 분석). 호출자가 소유권을 함께 받도록 강제해
	// 그 상황 자체를 없앤다.
	static std::shared_ptr<Material> InstantiateShared(const Material* origin, std::string_view newName = {});

//initialize material chainable functions
public:
	Material& SetBaseColor(Mathf::Color3 color);
	Material& SetBaseColor(float r, float g, float b);
	Material& SetMetallic(float metallic);
	Material& SetRoughness(float roughness);

public:
	Material& UseBaseColorMap(Texture* texture);
	Material& UseNormalMap(Texture* texture);
	Material& UseBumpMap(Texture* texture);
	Material& UseOccRoughMetalMap(Texture* texture);
	Material& UseAOMap(Texture* texture);
	Material& UseEmissiveMap(Texture* texture);
	Material& ConvertToLinearSpace(bool32 convert);
	Material& SetWindVector(const Mathf::Vector4& windVector);
	Material& SetUVScroll(const Mathf::Vector2& uvScroll);

	void SetShaderPSO(std::shared_ptr<ShaderPSO> pso);
	std::shared_ptr<ShaderPSO> GetShaderPSO() const;
	void ClearShaderPSO();

	// ���� Typed setters/getters (explicit cb/var) ����
	bool TrySetFloat(std::string_view cb, std::string_view var, float v);
	bool TryGetFloat(std::string_view cb, std::string_view var, float& out) const;

	bool TrySetInt(std::string_view cb, std::string_view var, int32_t v);
	bool TryGetInt(std::string_view cb, std::string_view var, int32_t& out) const;

	bool TrySetBool(std::string_view cb, std::string_view var, bool v);
	bool TryGetBool(std::string_view cb, std::string_view var, bool& out) const;

	bool TrySetVector(std::string_view cb, std::string_view var, const Mathf::Vector2& v);
	bool TrySetVector(std::string_view cb, std::string_view var, const Mathf::Vector3& v);
	bool TrySetVector(std::string_view cb, std::string_view var, const Mathf::Vector4& v);
	bool TryGetVector(std::string_view cb, std::string_view var, Mathf::Vector4& out) const; // �ִ� 4���� ��ȯ

	bool TrySetMatrix(std::string_view cb, std::string_view var, const Mathf::xMatrix& m);
	bool TryGetMatrix(std::string_view cb, std::string_view var, Mathf::xMatrix& out) const;

	bool TrySetValue(std::string_view cb, std::string_view var, const void* src, size_t size);

	// ���� Qualified name sugar: "CB.Var" ����
	bool TrySetFloat(std::string_view qualified, float v);
	bool TryGetFloat(std::string_view qualified, float& out) const;
	bool TrySetInt(std::string_view qualified, int32_t v);
	bool TryGetInt(std::string_view qualified, int32_t& out) const;
	bool TrySetBool(std::string_view qualified, bool v);
	bool TryGetBool(std::string_view qualified, bool& out) const;
	bool TrySetVector(std::string_view qualified, const Mathf::Vector2& v);
	bool TrySetVector(std::string_view qualified, const Mathf::Vector3& v);
	bool TrySetVector(std::string_view qualified, const Mathf::Vector4& v);
	bool TryGetVector(std::string_view qualified, Mathf::Vector4& out) const;
	bool TrySetMatrix(std::string_view qualified, const Mathf::xMatrix& m);
	bool TryGetMatrix(std::string_view qualified, Mathf::xMatrix& out) const;

	// ���� Ŀ���� PSO��: �������� CB�� GPU �ݿ� ����
	void ApplyShaderParams(ID3D11DeviceContext* ctx);
	void TrySetMaterialInfo();

	void UpdateCBufferView();

private:
	struct VarView {
		const ShaderPSO::CBEntry* cb{};
		const ShaderPSO::VariableDesc* var{};
	};
	VarView FindVar(std::string_view cb, std::string_view var) const;
	static bool SplitQualified(std::string_view q, std::string& outCB, std::string& outVar);

	bool WriteBytes(const VarView& v, const void* src, size_t size);
	bool ReadBytes(const VarView& v, void* dst, size_t size) const;

public:
    [[Property]]
	std::string m_name{};
	[[Property]]
	std::string m_baseColorTexName{};
	Texture* m_pBaseColor{ nullptr };
	[[Property]]
	std::string m_normalTexName{};
	Texture* m_pNormal{ nullptr };
	[[Property]]
	std::string m_ORM_TexName{};
	Texture* m_pOccRoughMetal{ nullptr };
	[[Property]]
	std::string m_AO_TexName{};
	Texture* m_AOMap{ nullptr };
	[[Property]]
	std::string m_EmissiveTexName{};
	Texture* m_pEmissive{ nullptr };
    [[Property]]
	MaterialInfomation m_materialInfo;
	[[Property]]
	MaterialFlowInformation m_flowInfo;
    [[Property]]
	FileGuid m_fileGuid{};
    [[Property]]
	MaterialRenderingMode m_renderingMode{ MaterialRenderingMode::Opaque };
	HashedGuid m_materialGuid{ make_guid() };
    std::shared_ptr<ShaderPSO> m_shaderPSO{ nullptr };
    [[Property]]
    std::string m_shaderPSOName{};
    const std::unordered_map<std::string, ShaderPSO::CBEntry>* m_cbMeta{ nullptr };
    std::unordered_map<std::string, std::vector<uint8_t>> m_cbufferValues{};
	std::unordered_map<std::string, std::vector<uint8_t>> m_cbufferView{};
	std::unordered_set<std::string> m_dirtyCBs;
	std::unordered_set<std::string> m_viewDirtyCBs;
};

