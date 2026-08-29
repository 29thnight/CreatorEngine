#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "MaterialInfomation.h"
#include "MaterialFlowInformation.h"
// 직접 포함해야 한다. Texture.h를 거쳐 들어오길 기대하면 안 되는데,
// Texture.h는 본문 전체가 #ifndef DYNAMICCPP_EXPORTS로 막혀 있어
// 스크립트 DLL 빌드에서는 Diagnostics가 선언되지 않는다.
#include "EngineResourceCensus.h"
#include "ShaderMetaHandle.h"
#include "Texture.h"
#include <mathematics/matrix4x4.hpp>
#include <mathematics/vector2.hpp>
#include <mathematics/vector3.hpp>
#include <mathematics/vector4.hpp>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>
#include <cstdint>

struct ShaderMeta;
struct ShaderMetaBindingLayout;
struct ShaderMetaPropertyBinding;

enum class MaterialRenderingMode
{
	Opaque,
	Transparent,
};

// Material의 디스크 정본은 ShaderMeta GUID와 이름 기반 논리 값이다. GPU byte
// offset은 Slang reflection 결과이므로 저장하지 않고 ConfigureShaderProperties에서
// 매 세대 다시 만든다. 한 타입만 유효하지만 variant를 YAML 정본에 끌어들이지
// 않도록 고정된 필드로 둔다.
struct MaterialPropertyValue
{
    static consteval auto reflect()
    {
        using Self = MaterialPropertyValue;
        return meta::schema<Self>(
            meta::field<&Self::m_name>,
            meta::field<&Self::m_numericValue>,
            meta::field<&Self::m_integerValue>,
            meta::field<&Self::m_boolValue>,
            meta::field<&Self::m_textureGuid>);
    }

    std::string m_name{};
    std::vector<float> m_numericValue{};
    std::int32_t m_integerValue{};
    bool m_boolValue{};
    FileGuid m_textureGuid{};
};

// M6-P2d-c runtime-only texture generation owner. 디스크 정본은
// MaterialPropertyValue의 (name, FileGuid)이고, 이 벡터는 임의 ShaderMeta texture
// property 이름을 실제 Texture generation 수명과 결합한다.
struct MaterialTextureOwner
{
	std::string propertyName{};
	std::shared_ptr<Texture> textureOwner{};
};

class Material : private Diagnostics::CountedResource<Diagnostics::EngineResource::Material>
{
   public:
   static consteval auto reflect()
   {
       using Self = Material;
       return meta::schema<Self>(
           meta::field<&Self::m_name>,
           meta::field<&Self::m_baseColorTexName>,
           meta::field<&Self::m_normalTexName>,
           meta::field<&Self::m_ORM_TexName>,
           meta::field<&Self::m_AO_TexName>,
           meta::field<&Self::m_EmissiveTexName>,
           meta::field<&Self::m_materialInfo>,
           meta::field<&Self::m_flowInfo>,
           meta::field<&Self::m_shaderMetaGuid>,
           meta::field<&Self::m_propertyValues>,
           meta::field<&Self::m_keywordSelections>,
           meta::field<&Self::m_fileGuid>,
           meta::field<&Self::m_renderingMode>);
   }
public:
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
	Material& SetBaseColor(math::vector3 color);
	Material& SetBaseColor(float r, float g, float b);
	Material& SetMetallic(float metallic);
	Material& SetRoughness(float roughness);

public:
	// Material이 Texture generation을 직접 소유한다. 호출자는 raw alias를 저장하지
	// 않고 아래 shared getter에서 필요한 순간 view만 얻는다.
	Material& UseBaseColorMap(std::shared_ptr<Texture> texture);
	Material& UseNormalMap(std::shared_ptr<Texture> texture);
	Material& UseBumpMap(std::shared_ptr<Texture> texture);
	Material& UseOccRoughMetalMap(std::shared_ptr<Texture> texture);
	Material& UseAOMap(std::shared_ptr<Texture> texture);
	Material& UseEmissiveMap(std::shared_ptr<Texture> texture);
	Material& UseTextureMap(std::string_view property, std::shared_ptr<Texture> texture);

	const std::shared_ptr<Texture>& GetTextureMapShared(
		std::string_view property) const noexcept;
	std::span<const MaterialTextureOwner> GetTextureOwners() const noexcept
	{
		return m_textureOwners;
	}
	const std::shared_ptr<Texture>& GetBaseColorMapShared() const noexcept;
	const std::shared_ptr<Texture>& GetNormalMapShared() const noexcept;
	const std::shared_ptr<Texture>& GetOccRoughMetalMapShared() const noexcept;
	const std::shared_ptr<Texture>& GetAOMapShared() const noexcept;
	const std::shared_ptr<Texture>& GetEmissiveMapShared() const noexcept;

	Material& ConvertToLinearSpace(bool32 convert);
	Material& SetWindVector(const math::vector4& windVector);
	Material& SetUVScroll(const math::vector2& uvScroll);

	// M5: ShaderMeta의 논리 schema와 M7의 Slang reflection layout을 결합한다.
	// Material은 layout의 소유 복사본을 공유하므로 meta/cache 주소 수명에 기대지 않는다.
	bool ConfigureShaderProperties(const ShaderMeta& meta,
		const ShaderMetaBindingLayout& layout, std::string& outError,
		ShaderMetaHandle shaderMetaHandle);
	ShaderMetaHandle GetShaderMetaHandle() const { return m_shaderMetaHandle; }
	const ShaderMetaBindingLayout* GetShaderBindingLayout() const;
	std::span<const std::uint8_t> GetConstantBufferData() const;
	// runtime schema를 Material에 설치하지 않고도 현재 논리 property를 reflection
	// layout에 맞춰 소유 byte block으로 만든다. RT frame sealing은 이 const API만
	// 사용하므로 Material*가 draw packet으로 새지 않는다.
	bool BuildShaderPropertyBlock(const ShaderMeta& meta,
		const ShaderMetaBindingLayout& layout,
		std::vector<std::uint8_t>& outBytes, std::string& outError) const;
	bool TrySetTextureGuid(std::string_view property, const FileGuid& guid);
	bool TryGetTextureGuid(std::string_view property, FileGuid& outGuid) const;
	bool TrySetKeywordSelection(std::string_view axis, std::string_view value);
	std::span<const std::uint16_t> GetKeywordSelections() const
	{
		return m_keywordSelections;
	}

	// ���� Typed setters/getters (explicit cb/var) ����
	bool TrySetFloat(std::string_view cb, std::string_view var, float v);
	bool TryGetFloat(std::string_view cb, std::string_view var, float& out) const;

	bool TrySetInt(std::string_view cb, std::string_view var, int32_t v);
	bool TryGetInt(std::string_view cb, std::string_view var, int32_t& out) const;

	bool TrySetBool(std::string_view cb, std::string_view var, bool v);
	bool TryGetBool(std::string_view cb, std::string_view var, bool& out) const;

	bool TrySetVector(std::string_view cb, std::string_view var, const math::vector2& v);
	bool TrySetVector(std::string_view cb, std::string_view var, const math::vector3& v);
	bool TrySetVector(std::string_view cb, std::string_view var, const math::vector4& v);
	bool TryGetVector(std::string_view cb, std::string_view var, math::vector4& out) const; // �ִ� 4���� ��ȯ

	bool TrySetMatrix(std::string_view cb, std::string_view var, const math::matrix4x4& m);
	bool TryGetMatrix(std::string_view cb, std::string_view var, math::matrix4x4& out) const;

	bool TrySetValue(std::string_view cb, std::string_view var, const void* src, size_t size);

	// ���� Qualified name sugar: "CB.Var" ����
	bool TrySetFloat(std::string_view qualified, float v);
	bool TryGetFloat(std::string_view qualified, float& out) const;
	bool TrySetInt(std::string_view qualified, int32_t v);
	bool TryGetInt(std::string_view qualified, int32_t& out) const;
	bool TrySetBool(std::string_view qualified, bool v);
	bool TryGetBool(std::string_view qualified, bool& out) const;
	bool TrySetVector(std::string_view qualified, const math::vector2& v);
	bool TrySetVector(std::string_view qualified, const math::vector3& v);
	bool TrySetVector(std::string_view qualified, const math::vector4& v);
	bool TryGetVector(std::string_view qualified, math::vector4& out) const;
	bool TrySetMatrix(std::string_view qualified, const math::matrix4x4& m);
	bool TryGetMatrix(std::string_view qualified, math::matrix4x4& out) const;

	void TrySetMaterialInfo();

private:
	struct VarView {
		const ShaderMetaPropertyBinding* binding{};
		std::size_t propertyIndex{ static_cast<std::size_t>(-1) };
	};
	VarView FindVar(std::string_view cb, std::string_view var) const;
	VarView FindProperty(std::string_view property) const;
	static bool SplitQualified(std::string_view q, std::string& outCB, std::string& outVar);

	bool WriteBytes(const VarView& v, const void* src, size_t size);
	bool ReadBytes(const VarView& v, void* dst, size_t size) const;

	struct RuntimeSchema;

public:
	std::string m_name{};
	std::string m_baseColorTexName{};
	std::string m_normalTexName{};
	std::string m_ORM_TexName{};
	std::string m_AO_TexName{};
	std::string m_EmissiveTexName{};
	MaterialInfomation m_materialInfo;
	MaterialFlowInformation m_flowInfo;
	FileGuid m_shaderMetaGuid{};
	std::vector<MaterialPropertyValue> m_propertyValues{};
	std::vector<std::uint16_t> m_keywordSelections{};
	FileGuid m_fileGuid{};
	MaterialRenderingMode m_renderingMode{ MaterialRenderingMode::Opaque };
	HashedGuid m_materialGuid{ make_guid() };
	// typed setter/getter와 legacy payload 왕복을 위한 CPU byte view. 값의 저장
	// 정본은 위 m_propertyValues이고 제품 draw packet은 그 정본에서 다시 pack한다.
    std::unordered_map<std::string, std::vector<uint8_t>> m_cbufferValues{};

private:
	friend class DataSystem;
	void ResetShaderRuntime();
	void ResetTextureRuntime();
	std::shared_ptr<const RuntimeSchema> m_runtimeSchema{};
	// runtime-only. GUID는 디스크 정본이고 이 값은 적용한 cache generation이다.
	ShaderMetaHandle m_shaderMetaHandle{};
	std::vector<MaterialTextureOwner> m_textureOwners{};
};

