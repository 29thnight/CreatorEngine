#include "DecalComponent.h"
#include "Texture.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include "Scene.h"

void DecalComponent::Awake()
{
    auto scene = GetOwner()->m_ownerScene;
    auto renderScene = SceneManagers->GetRenderScene();
    if (scene)
    {
        scene->CollectDecalComponent(this);
        if (renderScene)
            renderScene->RegisterCommand(this);
    }

	SetDecalTexture(m_diffusefileName.c_str());
    SetNormalTexture(m_normalFileName.c_str());
    SetORMTexture(m_ormFileName.c_str());
}

void DecalComponent::Update(float deltaSeconds)
{
    if (GetOwner()->IsEnabled() == false || useAnimation == false) return;

    timer += deltaSeconds;
    while (timer >= slicePerSeconds) {
        timer -= slicePerSeconds;
		sliceNumber++;
    }
    if (isLoop)
        sliceNumber = sliceNumber % (sliceX * sliceY);
    else
        sliceNumber = sliceX * sliceY - 1;
}

void DecalComponent::OnDestroy()
{
    auto scene = GetOwner()->m_ownerScene;
    auto renderScene = SceneManagers->GetRenderScene();
    if (scene)
    {
        scene->UnCollectDecalComponent(this);
        if(renderScene)
            renderScene->UnregisterCommand(this);
    }
}

void DecalComponent::SetDecalTexture(const std::string_view& fileName)
{
	file::path filename = fileName;
	file::path filepath = PathFinder::Relative("Textures\\") / filename.filename();
	m_decalTextureOwner = Texture::LoadSharedFromPath(filepath.string());
	m_decalTexture = m_decalTextureOwner.get();
    m_diffusefileName = fileName;
}

void DecalComponent::SetDecalTexture(const FileGuid& fileGuid)
{
}

void DecalComponent::SetNormalTexture(const std::string_view& fileName)
{
    file::path filename = fileName;
    file::path filepath = PathFinder::Relative("Textures\\") / filename.filename();
	m_normalTextureOwner = Texture::LoadSharedFromPath(filepath.string());
	m_normalTexture = m_normalTextureOwner.get();
    m_normalFileName = fileName;
}

void DecalComponent::SetNormalTexture(const FileGuid& fileGuid)
{
}

void DecalComponent::SetORMTexture(const std::string_view& fileName)
{
    file::path filename = fileName;
    file::path filepath = PathFinder::Relative("Textures\\") / filename.filename();
	m_ormTextureOwner = Texture::LoadSharedFromPath(filepath.string());
	m_occluroughmetalTexture = m_ormTextureOwner.get();
    m_ormFileName = fileName;
}

void DecalComponent::SetORMTexture(const FileGuid& fileGuid)
{
}
