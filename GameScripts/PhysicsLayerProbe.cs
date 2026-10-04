namespace CreatorEngine.Scripts;

public sealed class PhysicsLayerProbe : Component
{
    [EngineCallable]
    public static string BodyState()
    {
        var body = Entity.Find("LayerGateBody").GetComponent<PhysicsBodyComponent>();

        if (body is null || body.ReadState(out var state) != PhysicsError.None)
            throw new InvalidOperationException("Layer gate body is unavailable");

        return FormattableString.Invariant(
            $"{{\"componentId\":\"{body.ComponentId}\",\"y\":{state.Position.Y},\"velocityY\":{state.LinearVelocity.Y}}}");
    }

    [EngineCallable]
    public static string ResetBody()
    {
        var owner = Entity.Find("LayerGateBody");
        var body = owner.GetComponent<PhysicsBodyComponent>();

        if (body is null || owner.Transform is not { } transform)
            throw new InvalidOperationException("Layer gate body is unavailable");

        transform.WorldPosition = new Float3(4, 4, 0);

        return body.SetVelocity(default).ToString();
    }
}
