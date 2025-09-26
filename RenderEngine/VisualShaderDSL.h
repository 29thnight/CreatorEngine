//#pragma once
//
//#include <string>
//#include <string_view>
//#include <unordered_map>
//#include <vector>
//
//struct VisualShaderNodeDesc
//{
//    std::string id;
//    std::string type;
//    std::unordered_map<std::string, std::string> properties;
//};
//
//struct VisualShaderConnectionDesc
//{
//    std::string fromNode;
//    std::string fromSlot;
//    std::string toNode;
//    std::string toSlot;
//};
//
//struct VisualShaderGraphDesc
//{
//    std::string name;
//    std::string tag;
//    std::string queueTag;
//    std::vector<std::string> keywords;
//    std::string shaderAssetPath;
//    std::vector<VisualShaderNodeDesc> nodes;
//    std::vector<VisualShaderConnectionDesc> connections;
//};
//
//bool ParseVisualShaderDSL(std::string_view src, VisualShaderGraphDesc& out);
