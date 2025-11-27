# PhysicsManager

**Header:** `ScriptBinder/PhysicsManager.h`

**Inheritance:** `: public DLLCore::Singleton<PhysicsManager>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `void Initialize();`
- `void Update(float fixedDeltaTime);`
- `void Shutdown();`
- `void ChangeScene();`
- `[[maybe_unused]] void OnLoadScene();`
- `void OnUnloadScene();`
- `void ProcessCallback();`
- `void RayCast(RayEvent& rayEvent);`
- `bool Raycast(RayEvent& rayEvent, RaycastHit& hit);`
- `int Raycast(RayEvent& rayEvent, std::vector<RaycastHit>& hits);`
- `int BoxSweep(const SweepInput& in, const DirectX::SimpleMath::Vector3& boxExtent, std::vector<HitResult>& out_hits);`
- `int SphereSweep(const SweepInput& in, float radius, std::vector<HitResult>& out_hits);`
- `int CapsuleSweep(const SweepInput& in, float radius, float halfHeight, std::vector<HitResult>& out_hits);`
- `int BoxOverlap(const OverlapInput& in, const DirectX::SimpleMath::Vector3& boxExtent, std::vector<HitResult>& out_hits);`
- `int SphereOverlap(const OverlapInput& in, float radius, std::vector<HitResult>& out_hits);`
- `int CapsuleOverlap(const OverlapInput& in, float radius, float halfHeight, std::vector<HitResult>& out_hits);`
- `void SaveCollisionMatrix();`
- `void LoadCollisionMatrix();`
- `void SetRigidBodyState(const RigidBodyState& state);`
- `bool IsRigidBodyKinematic(unsigned int id) const;`
- `bool IsRigidBodyTrigger(unsigned int id) const;`
- `bool IsRigidBodyColliderEnabled(unsigned int id) const;`
- `bool IsRigidBodyUseGravity(unsigned int id) const;`
- `void ApplyForcedMoveToCCT(UINT controllerId, const DirectX::SimpleMath::Vector3& initialVelocity);`
- `void StopForcedMoveOnCCT(UINT controllerId);`
- `bool IsInForcedMove(UINT controllerId) const;`
- `void SetControllerPosition(UINT id, const DirectX::SimpleMath::Vector3& pos);`

## Public Properties
- `friend class Scene;`
- `using ColliderID = unsigned int;`
