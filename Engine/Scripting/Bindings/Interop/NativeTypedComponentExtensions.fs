namespace CreatorEngine

open CreatorEngine.NativeTypes

/// Typed reflection members for every registered native component wrapper.
[<AutoOpen>]
module NativeTypedComponentExtensions =
    type Animator with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = AnimatorView(NativeTypeView.FromNative(this.Reflected))

    type AudioListenerComponent with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = AudioListenerComponentView(NativeTypeView.FromNative(this.Reflected))

    type BehaviorTreeComponent with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = BehaviorTreeComponentView(NativeTypeView.FromNative(this.Reflected))

    type BoneComponent with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = BoneComponentView(NativeTypeView.FromNative(this.Reflected))

    type CameraComponent with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = CameraComponentView(NativeTypeView.FromNative(this.Reflected))

    type Canvas with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = CanvasView(NativeTypeView.FromNative(this.Reflected))

    type CharacterMovementComponent with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = CharacterMovementComponentView(NativeTypeView.FromNative(this.Reflected))

    type DecalComponent with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = DecalComponentView(NativeTypeView.FromNative(this.Reflected))

    type FoliageComponent with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = FoliageComponentView(NativeTypeView.FromNative(this.Reflected))

    type ImageComponent with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = ImageComponentView(NativeTypeView.FromNative(this.Reflected))

    type InvalidScriptComponent with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = InvalidScriptComponentView(NativeTypeView.FromNative(this.Reflected))

    type LightComponent with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = LightComponentView(NativeTypeView.FromNative(this.Reflected))

    type MeshRenderer with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = MeshRendererView(NativeTypeView.FromNative(this.Reflected))

    type PhysicsBodyComponent with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = PhysicsBodyComponentView(NativeTypeView.FromNative(this.Reflected))

    type PlayerInputComponent with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = PlayerInputComponentView(NativeTypeView.FromNative(this.Reflected))

    type RectTransformComponent with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = RectTransformComponentView(NativeTypeView.FromNative(this.Reflected))

    type SceneRenderProfileComponent with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = SceneRenderProfileComponentView(NativeTypeView.FromNative(this.Reflected))

    type ScriptComponent with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = ScriptComponentView(NativeTypeView.FromNative(this.Reflected))

    type SoundComponent with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = SoundComponentView(NativeTypeView.FromNative(this.Reflected))

    type SpriteRenderer with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = SpriteRendererView(NativeTypeView.FromNative(this.Reflected))

    type SpriteSheetComponent with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = SpriteSheetComponentView(NativeTypeView.FromNative(this.Reflected))

    type StateMachineComponent with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = StateMachineComponentView(NativeTypeView.FromNative(this.Reflected))

    type TerrainComponent with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = TerrainComponentView(NativeTypeView.FromNative(this.Reflected))

    type TextComponent with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = TextComponentView(NativeTypeView.FromNative(this.Reflected))

    type Transform with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = TransformView(NativeTypeView.FromNative(this.Reflected))

    type UIButton with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = UIButtonView(NativeTypeView.FromNative(this.Reflected))

    type UIComponent with
        /// Capture this native component instance and expose its typed reflected members.
        member this.Native = UIComponentView(NativeTypeView.FromNative(this.Reflected))
