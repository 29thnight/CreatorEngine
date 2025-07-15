#include "InverseKinematic.h"
#include "pch.h"
inline XMVECTOR LookRotationUnityLike(FXMVECTOR forward, FXMVECTOR up)
{
    // 정규화
    XMVECTOR f = XMVector3Normalize(forward);
    XMVECTOR r = XMVector3Normalize(XMVector3Cross(up, f));  // Unity 방식: right = up x forward
    XMVECTOR u = XMVector3Cross(f, r);                        // 보정된 up

    // 회전 행렬 구성
    XMMATRIX rotMatrix = XMMATRIX(
        r.m128_f32[0], r.m128_f32[1], r.m128_f32[2], 0.0f,
        u.m128_f32[0], u.m128_f32[1], u.m128_f32[2], 0.0f,
        f.m128_f32[0], f.m128_f32[1], f.m128_f32[2], 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );

    return XMQuaternionRotationMatrix(rotMatrix);
}

inline void RotateAround(Vector3& position, Quaternion& rotation, const Vector3& pivot, const Vector3& axis, float angleDegrees)
{
    // 1. 회전 각도를 라디안으로 변환
    float angleRadians = XMConvertToRadians(angleDegrees);

    // 2. 축과 각도로 회전 쿼터니언 생성
    Quaternion q = Quaternion::CreateFromAxisAngle(axis, angleRadians);

    // 3. 위치 회전
    Vector3 dir = position - pivot;         // 피벗에서의 상대 위치
    dir = Vector3::Transform(dir, q);       // 회전
    position = pivot + dir;                 // 새 위치

    // 4. 회전도 적용
    rotation = q * rotation;                // 쿼터니언 곱: q → rotation 방향으로 적용
    rotation.Normalize();
}

void InverseKinematic::Start()
{
	firstBone = GameObject::Find("first");
	secondBone = GameObject::Find("second");
	thirdBone = GameObject::Find("third");
	target = GameObject::Find("target");
	pole = GameObject::Find("pole");

    firstBoneEulerAngleOffset *= Mathf::Deg2Rad;
    secondBoneEularAngleOffset *= Mathf::Deg2Rad;
    thirdBoneEularAngleOffset *= Mathf::Deg2Rad;
}

void InverseKinematic::Update(float tick)
{
}

void InverseKinematic::LateUpdate(float tick)
{
    Vector3 towardPole = pole->m_transform.GetWorldPosition() - firstBone->m_transform.GetWorldPosition();
    Vector3 towardTarget = target->m_transform.GetWorldPosition() - firstBone->m_transform.GetWorldPosition();
    towardPole.Normalize();
    towardTarget.Normalize();

    float rootBoneLength = Vector3::Distance(firstBone->m_transform.GetWorldPosition(), secondBone->m_transform.GetWorldPosition());
    float secondBoneLength = Vector3::Distance(secondBone->m_transform.GetWorldPosition(), thirdBone->m_transform.GetWorldPosition());
    float totalChainLength = rootBoneLength + secondBoneLength;

    // Align root with target

    //auto temp = Quaternion::LookRotation(towardTarget, towardPole);
    auto temp = LookRotationUnityLike(towardTarget, towardPole);
    firstBone->m_transform.SetWorldRotation(temp);
    auto fQua = Quaternion::CreateFromYawPitchRoll(
        firstBoneEulerAngleOffset.y, 
        firstBoneEulerAngleOffset.x, 
        firstBoneEulerAngleOffset.z);
    fQua.Normalize();
    firstBone->m_transform.AddRotation(fQua);

    Vector3 towardSecondBone = secondBone->m_transform.GetWorldPosition() - firstBone->m_transform.GetWorldPosition();
    towardSecondBone.Normalize();

    float targetDistance = Vector3::Distance(firstBone->m_transform.GetWorldPosition(), target->m_transform.GetWorldPosition());

    // Limit hypotenuse to under the total bone distance to prevent invalid triangles
    targetDistance = std::min(targetDistance, totalChainLength * 0.9999f);

    // Solve for the angle for the root bone
    // See https://en.wikipedia.org/wiki/Law_of_cosines
    float adjacent =
        (
            (rootBoneLength * rootBoneLength) +
            (targetDistance * targetDistance) -
            (secondBoneLength * secondBoneLength)
            ) / (2 * targetDistance * rootBoneLength);
    float angle = std::acos(adjacent) * Mathf::Rad2Deg;

    // We rotate around the vector orthogonal to both pole and second bone
    Vector3 cross = towardPole.Cross(towardSecondBone);

    // We've rotated the root bone to the right place, so we just 
    // look at the target from the elbow to get the final rotation
    Vector3 v1 = target->m_transform.GetWorldPosition() - secondBone->m_transform.GetWorldPosition();
    v1.Normalize();
    Quaternion secondBoneTargetRotation = LookRotationUnityLike(v1, cross);
    auto sQua = Quaternion::CreateFromYawPitchRoll(
        secondBoneEularAngleOffset.y,
        secondBoneEularAngleOffset.x,
        secondBoneEularAngleOffset.z
    );
    sQua.Normalize();
    secondBoneTargetRotation = secondBoneTargetRotation * sQua;
    secondBone->m_transform.SetWorldRotation(secondBoneTargetRotation);

    if (alignThirdBoneWithTargetRotation)
    {
        thirdBone->m_transform.SetWorldRotation(target->m_transform.GetWorldQuaternion());
        thirdBone->m_transform.AddRotation(Quaternion::CreateFromYawPitchRoll(
            thirdBoneEularAngleOffset.y,
            thirdBoneEularAngleOffset.x,
            thirdBoneEularAngleOffset.z
        ));
    }
}


