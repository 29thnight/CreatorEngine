// Checked-in accessor source for Assets/InputGraph/Gameplay.inputgraph.
// IDs and interface fingerprint follow the canonical compiler's sorted schema.
// Source-reviewed only: re-export from LX and verify in the native/AOT gates.
namespace CreatorEngine.Generated
{
    public static class GameplayInputs
    {
        public static readonly InputSignal<Float2> Move = InputSignal.Vector2(
            new InputID(0x9ad58e309ff74f2aUL, 0xa8c2435a3c126801UL),
            new InputID(0x3217cd6aa3e5272bUL, 10), 0x18afb5ca5ee64670UL);
        public static readonly InputSignal<bool> Jump = InputSignal.Button(
            new InputID(0x9ad58e309ff74f2aUL, 0xa8c2435a3c126801UL),
            new InputID(0x3217cd6aa3e5272bUL, 20), 0x18afb5ca5ee64670UL);
        public static readonly InputSignal<Float2> Look = InputSignal.Vector2(
            new InputID(0x9ad58e309ff74f2aUL, 0xa8c2435a3c126801UL),
            new InputID(0x3217cd6aa3e5272bUL, 30), 0x18afb5ca5ee64670UL);
        public static class Layers
        {
            public static readonly InputID Gameplay = new(0x3217cd6aa3e5272bUL, 1);
        }
    }
}
