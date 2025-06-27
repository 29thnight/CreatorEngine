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

    // �̸����� ���� �޼����
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
    // �̸������ �ӽ� �����͵�
    std::vector<TempEmitterInfo> m_tempEmitters;

    // ���� ���� ���� ������
    std::shared_ptr<ParticleSystem> m_editingEmitter = nullptr;
    bool m_isEditingEmitter = false;

    // UI ����
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

    // UI ������ �޼����
    void RenderMainEditor();
    void RenderEmitterEditor();
    void RenderExistingModules();
    void RenderPreviewControls();

    // ������ ����/���� ����
    void StartCreateEmitter();
    void SaveCurrentEmitter();
    void CancelEditing();

    // ��� �߰�
    void AddSelectedModule();
    void AddSelectedRender();
};
