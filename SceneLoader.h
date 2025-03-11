#pragma once

//전 프로젝트 다옮겨둠 사용시 수정필요
// 
//#pragma once
//#include "fstream"
//#include <nlohmann/json.hpp>
//using json = nlohmann::json;
//
//class ObjectData
//{
//public:
//    std::string name;
//    std::string type; // enumclass 0 = basic 3 = UI 딴것도 쓸지는 모르겠음
//    DXMath::Vector3 position;
//    DXMath::Quaternion rotation;
//    DXMath::Vector3 scale;
//
//    // from_json 함수 정의
//    void from_json(const json& j)
//    {
//        name = j.at("objectName").get<std::string>();
//        type = j.at("objectType").get<std::string>();
//        float x = j["position"]["x"];
//        float y = j["position"]["y"];
//        float z = j["position"]["z"];
//
//        x *= 100.f; //이건 자체엔진 설정에따라 봐야함
//        y *= 100.f;
//        z *= -1.0f;
//        position = DXMath::Vector3(x, y, z);
//        float x2 = j["rotation"]["x"];
//        float z2 = j["rotation"]["z"];
//        z2 *= -1.0f;
//        rotation = DXMath::Quaternion(x2, j["rotation"]["y"], z2, j["rotation"]["w"]);
//        scale = DXMath::Vector3(j["scale"]["x"], j["scale"]["y"], j["scale"]["z"]);
//    }
//};
//
//// SceneData 클래스
//class SceneData
//{
//public:
//    SceneData() = default;
//    ~SceneData();
//    std::vector<ObjectData*> objDatas;
//
//    // from_json 함수 정의
//    void from_json(const json& j)
//    {
//        objNum = j["objects"].size();
//        for (const auto& obj : j["objects"])
//        {
//            ObjectData* data = new ObjectData;
//            data->from_json(obj);
//            objDatas.push_back(data);
//        }
//    }
//
//private:
//    int objNum;
//};
//
//class Scene;
//class SceneLoader
//{
//public:
//    SceneLoader() = default;
//    ~SceneLoader();
//
//    void Load(const std::string& _path);
//    void ImportUnityScene(std::string_view _path, Scene* _scene); // 파일 경로 , 내가 적용할 씬
//
//private:
//
//public:
//
//private:
//    std::unordered_map<std::string, SceneData*> sceneDatas;
//};
