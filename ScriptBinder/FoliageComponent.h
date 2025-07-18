#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "Mesh.h"

//struct FoliageType
//{
//	Mesh* m_mesh = nullptr;
//	bool m_castShadow = true;
//};
//
//struct FoliageInstance
//{
//	Mathf::Vector3 m_position;
//	Mathf::Vector3 m_rotation; // Euler angles
//	Mathf::Vector3 m_scale = { 1.0f, 1.0f, 1.0f };
//	uint32 m_foliageTypeID = 0; // ID to reference FoliageType
//};
//
//class FoliageComponent : public Component //작성 대기
//{
//public:
//	GENERATED_BODY(FoliageComponent)
//
//	void AddFoliageType(const FoliageType& type);
//	void RemoveFoliageType(uint32 typeID);
//
//	void AddFoliageInstance(const FoliageInstance& instance);
//	void RemoveFoliageInstance(size_t index);
//
//	const std::vector<FoliageType>& GetFoliageTypes() const { return m_foliageTypes; }
//	const std::vector<FoliageInstance>& GetFoliageInstances() const { return m_foliageInstances; }
//	std::vector<FoliageInstance>& GetMutableFoliageInstances() { return m_foliageInstances; }
//
//	bool Save(const file::path& filePath) const;
//	bool Load(const file::path& filePath);
//
//private:
//	FileGuid m_foliageAssetGuid; // Unique identifier for this foliage component
//	std::vector<FoliageType> m_foliageTypes; // List of foliage types
//	std::vector<FoliageInstance> m_foliageInstances; // List of foliage instances
//};