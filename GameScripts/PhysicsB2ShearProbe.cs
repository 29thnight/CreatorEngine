namespace CreatorEngine.Scripts;

public sealed partial class PhysicsB2ShearProbe : Component
{
    private float _elapsed;
    private bool _requested;

    public override void PostPhysics(float delta)
    {
        if (_requested || Environment.GetEnvironmentVariable("CE_PHYSICS_B2_SHEAR") != "1") return;

        _elapsed += delta;
        if (_elapsed < .2f) return;

        _requested = true;
        Entity.Parent.Transform!.LocalScale = new Float3(2,1,1);
        Transform.LocalRotation = new Quaternion(0,.38268343f,0,.92387953f);
        Console.WriteLine("[physics.player.b2.shear] requested=true");
    }
}
