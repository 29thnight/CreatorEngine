#include "RegisterReflect.h"
#include "LightComponent.h"
#include "SpriteComponent.h"
#include "RenderableComponents.h"
#include "ModuleBehavior.h"
#include "GameObject.h"
#include "Scene.h"

REFLECTION_REGISTER()
{
	AUTO_REGISTER_CLASS(LightComponent);
	AUTO_REGISTER_CLASS(SpriteComponent);
	AUTO_REGISTER_CLASS(MeshRenderer);
	AUTO_REGISTER_CLASS(Animator);
	AUTO_REGISTER_CLASS(ModuleBehavior);
	AUTO_REGISTER_CLASS(MeshRenderer);
	AUTO_REGISTER_CLASS(SpriteRenderer);
	AUTO_REGISTER_CLASS(Animator);
	AUTO_REGISTER_CLASS(LightMapping);
	AUTO_REGISTER_CLASS(GameObject);
	AUTO_REGISTER_CLASS(Object);
	AUTO_REGISTER_CLASS(Scene);
}