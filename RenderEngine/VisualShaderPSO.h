//#pragma once
//
//#include "ShaderDSL.h"
//#include "VisualShaderDSL.h"
//
//class ShaderPSO;
//
//#include <memory>
//#include <string>
//#include <string_view>
//#include <unordered_map>
//
//class VisualShaderPSO
//{
//public:
//    VisualShaderPSO(std::shared_ptr<ShaderPSO> basePso,
//                    VisualShaderGraphDesc graphDesc,
//                    ShaderAssetDesc baseDesc);
//
//    const std::shared_ptr<ShaderPSO>& GetBasePSO() const noexcept { return m_basePSO; }
//    const VisualShaderGraphDesc& GetGraphDesc() const noexcept { return m_graphDesc; }
//    const ShaderAssetDesc& GetBaseShaderDesc() const noexcept { return m_baseShaderDesc; }
//
//    const VisualShaderNodeDesc* FindNode(std::string_view nodeId) const;
//
//private:
//    std::shared_ptr<ShaderPSO> m_basePSO;
//    VisualShaderGraphDesc m_graphDesc;
//    ShaderAssetDesc m_baseShaderDesc;
//    std::unordered_map<std::string, std::size_t> m_nodeIndexById;
//};
