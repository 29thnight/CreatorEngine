// Auto-generated RegisterReflect.h

#include "Material.h"
#include "Component.h"
#include "GameObject.h"
#include "Mesh.h"
#include "ImageComponent.h"
#include "Canvas.h"
#include "LightComponent.h"
#include "Object.h"
#include "RenderableComponents.h"
#include "Transform.h"
#include "Scene.h"
#include "TextComponent.h"
#include "ReflectionMecro.h"
#include "UIButton.h"
#include "UIComponent.h"

#define AUTO_REGISTER_CLASS(ClassTypeName) \
    Meta::Register<ClassTypeName>();

void REFLECTION_REGISTER()
{
    AUTO_REGISTER_CLASS(Animator);
    AUTO_REGISTER_CLASS(Canvas);
    AUTO_REGISTER_CLASS(Component);
    AUTO_REGISTER_CLASS(GameObject);
    AUTO_REGISTER_CLASS(ImageComponent);
    AUTO_REGISTER_CLASS(LightComponent);
    AUTO_REGISTER_CLASS(LightMapping);
    AUTO_REGISTER_CLASS(Material);
    AUTO_REGISTER_CLASS(MaterialInfomation);
    AUTO_REGISTER_CLASS(Mesh);
    AUTO_REGISTER_CLASS(MeshRenderer);
    AUTO_REGISTER_CLASS(Object);
    AUTO_REGISTER_CLASS(Scene);
    AUTO_REGISTER_CLASS(SpriteRenderer);
    AUTO_REGISTER_CLASS(T);
    AUTO_REGISTER_CLASS(TextComponent);
    AUTO_REGISTER_CLASS(Transform);
    AUTO_REGISTER_CLASS(UIButton);
    AUTO_REGISTER_CLASS(UIComponent);
}
