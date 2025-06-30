#pragma once
#include "ParticleSystem.h"

enum class EffectModuleType
{
    SpawnModule,
    MeshSpawnModule,
    MovementModule,
    ColorModule,
    SizeModule,
};

enum class RenderType
{
    Billboard,
    Mesh,
};

struct TempEmitterInfo {
    std::shared_ptr<ParticleSystem> particleSystem;
    bool isPlaying = false;
    std::string name;
};

class EffectEditor
{
public:
    EffectEditor();
    void Release();

    void SetTexture(Texture* tex);
    void Render(RenderScene& scene, Camera& camera);
    void ExportToManager(const std::string& effectName);
    void Update(float delta);

    // 미리보기 전용 메서드들
    void PlayPreview();
    void StopPreview();
    void PlayEmitterPreview(int index);
    void StopEmitterPreview(int index);
    void RemoveEmitter(int index);
    void AssignTextureToEmitter(int emitterIndex, int textureIndex);

    // spawn module용 설정 메서드
    void StartModifyEmitter(int index);
    void SaveModifiedEmitter(const std::string& name = "");
    void CancelModifyEmitter();
    void RenderModuleDetailEditor();
    void RenderSpawnModuleEditor(SpawnModuleCS* spawnModule);
    void RenderMovementModuleEditor(MovementModuleCS* movementModule);
    void RenderColorModuleEditor(ColorModuleCS* colorModule);
    void RenderSizeModuleEditor(SizeModuleCS* sizeModule);

private:
    // 미리보기용 임시 에미터들
    std::vector<TempEmitterInfo> m_tempEmitters;
    char m_newEmitterName[256];
    bool m_emitterNameInitialized;

    // 현재 편집 중인 에미터
    std::shared_ptr<ParticleSystem> m_editingEmitter = nullptr;
    bool m_isEditingEmitter = false;

    // UI 관련
    int m_selectedModuleIndex = 0;
    int m_selectedRenderIndex = 0;
    std::vector<Texture*> m_textures;

    struct ModuleInfo {
        const char* name;
        EffectModuleType type;
    };

    struct RenderInfo {
        const char* name;
        RenderType type;
    };

    std::vector<ModuleInfo> m_availableModules = {
        {"Spawn Module", EffectModuleType::SpawnModule},
        {"Mesh Spawn Module", EffectModuleType::MeshSpawnModule},
        {"Movement Module", EffectModuleType::MovementModule},
        {"Color Module", EffectModuleType::ColorModule},
        {"Size Module", EffectModuleType::SizeModule},
    };

    std::vector<RenderInfo> m_availableRenders = {
        {"Billboard Render", RenderType::Billboard},
        {"Mesh Render", RenderType::Mesh},
    };

    // 모듈 편집 관련
    bool m_isModifyingEmitter = false;
    int m_modifyingEmitterIndex = -1;
    std::shared_ptr<ParticleSystem> m_modifyingSystem = nullptr;

    // 현재 선택된 모듈 정보
    int m_selectedModuleForEdit = -1;
    int m_selectedRenderForEdit = -1;



private:
    // UI 렌더링 메서드들
    void RenderMainEditor();
    void RenderEmitterEditor();
    void RenderExistingModules();
    void RenderPreviewControls();
    void RenderModifyEmitterEditor();
    void RenderTextureDragDropTarget();


    // 에미터 생성/편집 관련
    void StartCreateEmitter();
    void SaveCurrentEmitter(const std::string& name);
    void CancelEditing();

    // 모듈 추가
    void AddSelectedModule();
    void AddSelectedRender();

};
