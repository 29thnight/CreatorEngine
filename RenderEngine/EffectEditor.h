#pragma once
#include "ParticleSystem.h"
#include "EffectSerializer.h"

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

    // �̸����� ���� �޼����
    void PlayPreview();
    void StopPreview();
    void PlayEmitterPreview(int index);
    void StopEmitterPreview(int index);
    void RemoveEmitter(int index);
    void AssignTextureToEmitter(int emitterIndex, int textureIndex);
    void RenderUnifiedDragDropTarget();

    // 2D ���� ���� �޼���
    void StartModifyEmitter(int index);
    void SaveModifiedEmitter(const std::string& name = "");
    void CancelModifyEmitter();

    // ���� ���� ���� �޼���
    void RenderModuleDetailEditor();            // 2d
    void RenderRenderModuleDetailEditor();      // 3d

    // 2D ���� ���� �޼���
    void RenderSpawnModuleEditor(SpawnModuleCS* spawnModule);
    void RenderMovementModuleEditor(MovementModuleCS* movementModule);
    void RenderColorModuleEditor(ColorModuleCS* colorModule);
    void RenderSizeModuleEditor(SizeModuleCS* sizeModule);
    void RenderBillboardModuleGPUEditor(BillboardModuleGPU* billboardModule);

    // 3D ���� ���� �޼���
    void RenderMeshSpawnModuleEditor(MeshSpawnModuleCS* spawnModule);
    void RenderMeshModuleGPUEditor(MeshModuleGPU* meshModule);

private:
    // �̸������ �ӽ� �����͵�
    std::vector<TempEmitterInfo> m_tempEmitters;
    char m_newEmitterName[256];
    bool m_emitterNameInitialized;

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

    // ��� ���� ����
    bool m_isModifyingEmitter = false;
    int m_modifyingEmitterIndex = -1;
    std::shared_ptr<ParticleSystem> m_modifyingSystem = nullptr;

    // ���� ���õ� ��� ����
    int m_selectedModuleForEdit = -1;
    int m_selectedRenderForEdit = -1;

    // json ����
    char m_saveFileName[256] = "my_effect.json";
    char m_loadFileName[256] = "my_effect.json";
    bool m_showSaveDialog = false;
    bool m_showLoadDialog = false;

private:
    // UI ������ �޼����
    void RenderMainEditor();
    void RenderEmitterEditor();
    void RenderExistingModules();
    void RenderPreviewControls();
    void RenderModifyEmitterEditor();
    void RenderJsonSaveLoadUI();

    // ������ ����/���� ����
    void StartCreateEmitter();
    void SaveCurrentEmitter(const std::string& name);
    void CancelEditing();

    // ��� �߰�
    void AddSelectedModule();
    void AddSelectedRender();

    // json ����
    void SaveEffectToJson(const std::string& filename);
    void LoadEffectFromJson(const std::string& filename);
    void SyncResourcesFromLoadedEmitters();
    void AddTextureToEditorList(Texture* texture);
};
