#include "InverseKinematic.h"
#include "pch.h"

inline XMVECTOR QuaternionFromToRotation(XMVECTOR fromDirection, XMVECTOR toDirection)
{
    // 1. 입력 벡터 정규화
    fromDirection = XMVector3Normalize(fromDirection);
    toDirection = XMVector3Normalize(toDirection);

    // 2. 내적 계산
    float dot_product = XMVectorGetX(XMVector3Dot(fromDirection, toDirection));

    // 3. 특이 케이스: 거의 반대 방향 (180도 회전)
    if (dot_product < -0.999999f)
    {
        XMVECTOR temp_axis = XMVector3Cross(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), fromDirection);

        // from이 월드 X축과 평행한 경우, 월드 Y축 사용
        if (XMVectorGetX(XMVector3LengthSq(temp_axis)) < 0.000001f)
        {
            temp_axis = XMVector3Cross(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), fromDirection);
        }

        XMVECTOR rotation_axis = XMVector3Normalize(temp_axis);

        // 180도 회전은 w=0인 쿼터니언입니다.
        return rotation_axis; // DirectX에서는 w=0인 벡터를 순수 쿼터니언으로 취급
    }

    // 5. 일반적인 경우
    // 회전축
    XMVECTOR rotation_axis = XMVector3Cross(fromDirection, toDirection);

    // w (실수부) 계산
    // sqrt(len_sq(from) * len_sq(to))는 1.0이므로 (정규화됨), 1.0 + dot_product
    float w = 1.0f + dot_product;

    XMVECTOR q = XMVectorSetW(rotation_axis, w);

    // 6. 최종 쿼터니언 정규화
    return XMQuaternionNormalize(q);
}

inline XMVECTOR LookRotationUnityLike(FXMVECTOR forward, FXMVECTOR up)
{
    // 정규화
    XMVECTOR f = XMVector3Normalize(forward);
    XMVECTOR r = XMVector3Normalize(XMVector3Cross(up, f));  // Unity 방식: right = up x forward
    if (XMVectorGetX(XMVector3Length(r)) < 0.01f) {
        return QuaternionFromToRotation(XMVectorSet(0, 0, 1, 0), f);
    }

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
    
    if (XMVector3Equal(axis, XMVectorZero())) {
        return;
    }

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
    //towardPole.Normalize();
    //towardTarget.Normalize();

    float rootBoneLength = Vector3::Distance(firstBone->m_transform.GetWorldPosition(), secondBone->m_transform.GetWorldPosition());
    float secondBoneLength = Vector3::Distance(secondBone->m_transform.GetWorldPosition(), thirdBone->m_transform.GetWorldPosition());
    float totalChainLength = rootBoneLength + secondBoneLength;

    // Align root with target

    //auto temp = Quaternion::LookRotation(towardTarget, towardPole);
    auto temp = LookRotationUnityLike(towardTarget, towardPole);
    firstBone->m_transform.SetWorldRotation(temp);
    firstBone->m_transform.UpdateWorldMatrix();
    auto fQua = Quaternion::CreateFromYawPitchRoll(
        firstBoneEulerAngleOffset.y, 
        firstBoneEulerAngleOffset.x, 
        firstBoneEulerAngleOffset.z);
    //fQua.Normalize();
    firstBone->m_transform.AddRotation(fQua);
    thirdBone->m_transform.UpdateWorldMatrix();

    Vector3 towardSecondBone = secondBone->m_transform.GetWorldPosition() - firstBone->m_transform.GetWorldPosition();
    //towardSecondBone.Normalize();

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
    cross.Normalize();

    /*if (!std::isnan(angle)) {
        Vector3 t = { firstBone->m_transform.position.x, firstBone->m_transform.position.y, firstBone->m_transform.position.z };
        Quaternion q = firstBone->m_transform.rotation;
        RotateAround(t, q, t, cross, -angle);
        firstBone->m_transform.SetWorldPosition(t);
        firstBone->m_transform.SetWorldRotation(q);
    }*/
    thirdBone->m_transform.UpdateWorldMatrix();

    // We've rotated the root bone to the right place, so we just 
    // look at the target from the elbow to get the final rotation
    Vector3 v1 = target->m_transform.GetWorldPosition() - secondBone->m_transform.GetWorldPosition();
    //v1.Normalize();
    Quaternion secondBoneTargetRotation = LookRotationUnityLike(v1, cross);
    auto sQua = Quaternion::CreateFromYawPitchRoll(
        secondBoneEularAngleOffset.y,
        secondBoneEularAngleOffset.x,
        secondBoneEularAngleOffset.z
    );
    //sQua.Normalize();
    secondBoneTargetRotation = secondBoneTargetRotation * sQua;
    secondBone->m_transform.SetWorldRotation(secondBoneTargetRotation);
    thirdBone->m_transform.UpdateWorldMatrix();

    if (alignThirdBoneWithTargetRotation)
    {
        thirdBone->m_transform.SetWorldRotation(target->m_transform.GetWorldQuaternion());
        thirdBone->m_transform.UpdateWorldMatrix();
        thirdBone->m_transform.AddRotation(Quaternion::CreateFromYawPitchRoll(
            thirdBoneEularAngleOffset.y,
            thirdBoneEularAngleOffset.x,
            thirdBoneEularAngleOffset.z
        ));
        thirdBone->m_transform.UpdateWorldMatrix();
    }
}


