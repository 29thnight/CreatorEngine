namespace CreatorEngine
{
    public readonly record struct Entity(ObjectHandle Handle)
    {
        public bool IsAlive => Native.Alive;
    }
    public sealed class Component
    {
        public Entity Entity = new(new ObjectHandle(1, 1));
        public bool Enabled = true;
        public bool BeginSucceeded = true;
        public SimulationScope Scope { get; } = new();
    }
    public sealed class SimulationScope
    {
        private readonly List<Action> _cleanup = new();
        public void RegisterCleanup(Action cleanup) => _cleanup.Add(cleanup);
        public void Cancel()
        {
            foreach (var cleanup in _cleanup)
            {
                cleanup();
            }
            _cleanup.Clear();
        }
    }
    internal static class Native
    {
        internal static bool IsGameThread = true;
        internal static bool Alive = true;
        internal static InputSessionHandle Session = new(1, 1);
        internal static InputFrame? Frame;
        internal static ulong InterfaceHash = 77;
        internal static int Exceptions;
        internal static InputSessionHandle InputFindSession(ObjectHandle owner) => Alive ? Session : default;
        internal static InputFrame InputCopyFrame(InputSessionHandle session, InputDomain domain) => Frame!;
        internal static void InputRequest(InputSessionHandle session, NativeInputRequest request) { }
        internal static InputDevice[] InputListDevices() => Array.Empty<InputDevice>();
        internal static void InputHaptic(InputSessionHandle session, float seconds, float left, float right) { }
        internal static void InputValidateSignal(InputSessionHandle session, InputID graph, InputID signal,
            ulong interfaceHash, InputValueType type, InputDomain domain)
        {
            if (interfaceHash != InterfaceHash)
            {
                throw new InvalidOperationException("Stale interface hash.");
            }
        }
        internal static void Log(int level, string text) { ++Exceptions; }
    }
}
