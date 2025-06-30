#pragma once
#include "ParticleSystem.h"
#include "Component.h"
#include "EffectComponent.generated.h"

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

class EffectComponent : public Component, public IAwakable, public IUpdatable, public IOnDistroy
{
public:
   ReflectEffectComponent
        [[Serializable(Inheritance:Component)]]
    GENERATED_BODY(EffectComponent)

    void Awake() override;
    void Update(float tick) override;
    void OnDistroy() override;
    void SetTexture(Texture* tex);
    void Render(RenderScene& scene, Camera& camera);
    void ExportToManager(const std::string& effectName);

    // 미리보기 전용 메서드들
    [[Method]]
    void PlayPreview();
    [[Method]]
    void StopPreview();
    [[Method]]
    void PlayEmitterPreview(int index);
    [[Method]]
    void StopEmitterPreview(int index);
    [[Method]]
    void RemoveEmitter(int index);
    [[Method]]
    void AssignTextureToEmitter(int emitterIndex, int textureIndex);

    [[Property]]
    int num = 0;

private:
    // 미리보기용 임시 에미터들
    std::vector<TempEmitterInfo> m_tempEmitters;

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

    // UI 렌더링 메서드들
    void RenderMainEditor();
    void RenderEmitterEditor();
    void RenderExistingModules();
    void RenderPreviewControls();

    // 에미터 생성/편집 관련
    void StartCreateEmitter();
    void SaveCurrentEmitter();
    void CancelEditing();

    // 모듈 추가
    void AddSelectedModule();
    void AddSelectedRender();
};
