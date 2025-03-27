//#include "SceneLoader.h"
//#include "TransformComponent.h"
//#include "Object.h"
//#include "Scene.h"
//#include "Helper.h"
//
//static std::unordered_map<std::string, Object::ObjectType> ObjectTypeMapping = // 매핑용
//{
//    { "Basic",       Object::ObjectType::Basic },
//    { "UI",          Object::ObjectType::UI  },
//    { "Camera",      Object::ObjectType::Camera },
//    { "Light",       Object::ObjectType::Light },
//    { "Background",  Object::ObjectType::Background },
//};
//
//SceneLoader::~SceneLoader()
//{
//    SafeExtinction::SAFE_CLEAR_CONTAINER(sceneDatas);
//}
//
//void SceneLoader::Load(const std::string& _path)
//{
//    std::ifstream inputFile(_path);
//    if (!inputFile.is_open())
//    {
//        std::cerr << "파일을 열 수 없습니다: " << _path << std::endl;
//        return;
//    }
//    else
//    {
//        std::cout << "파일이 열렸습니다: " << _path << std::endl;
//    }
//
//    json jsonData;
//    inputFile >> jsonData;
//    inputFile.close();
//
//    // SceneData에 JSON 데이터를 넣기
//    SceneData* sceneData = new SceneData;
//    sceneData->from_json(jsonData);
//    sceneDatas[_path] = sceneData;
//
//}
//
//void SceneLoader::ImportUnityScene(std::string_view _path, Scene* _scene)
//{
//    auto scene = sceneDatas.find(_path.data());
//    if (scene != sceneDatas.end())
//    {
//        for (auto& objData : scene->second->objDatas)
//        {
//            auto objTypeIter = ObjectTypeMapping.find(objData->type);
//            if (objTypeIter != ObjectTypeMapping.end())
//            {
//                auto gameobj = _scene->GetGameObject(objTypeIter->second, objData->name);
//                // std::string name = gameobj->GetName();
//                if (nullptr != gameobj)
//                {
//                    DXMath::Matrix translationMatrix = DXMath::Matrix::CreateTranslation(objData->position);
//                    DXMath::Matrix rotationMatrix = DXMath::Matrix::CreateFromQuaternion(objData->rotation);
//                    DXMath::Matrix scaleMatrix = DXMath::Matrix::CreateScale(objData->scale);
//
//                    //DXMath::Matrix finalMatrix = translationMatrix * rotationMatrix * scaleMatrix  ;
//                    //gameobj->GetComponent<TransformComponent>()->SetLocalMatrix(finalMatrix);
//
//                    gameobj->GetComponent<TransformComponent>()->SetPosition(objData->position);
//                    gameobj->GetComponent<TransformComponent>()->SetQuaternion(objData->rotation);
//                    gameobj->GetComponent<TransformComponent>()->SetScale(objData->scale);
//                }
//            }
//        }
//    }
//}
//
//SceneData::~SceneData()
//{
//    SafeExtinction::SAFE_CLEAR_CONTAINER(objDatas);
//}
