#include "InverseKinematic.h"
#include "pch.h"

inline XMVECTOR QuaternionFromToRotation(XMVECTOR fromDirection, XMVECTOR toDirection)
{
    // 1. �Է� ���� ����ȭ
    fromDirection = XMVector3Normalize(fromDirection);
    toDirection = XMVector3Normalize(toDirection);

    // 2. ���� ���
    float dot_product = XMVectorGetX(XMVector3Dot(fromDirection, toDirection));

    // 3. Ư�� ���̽�: ���� �ݴ� ���� (180�� ȸ��)
    if (dot_product < -0.999999f)
    {
        XMVECTOR temp_axis = XMVector3Cross(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), fromDirection);

        // from�� ���� X��� ������ ���, ���� Y�� ���
        if (XMVectorGetX(XMVector3LengthSq(temp_axis)) < 0.000001f)
        {
            temp_axis = XMVector3Cross(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), fromDirection);
        }

        XMVECTOR rotation_axis = XMVector3Normalize(temp_axis);

        // 180�� ȸ���� w=0�� ���ʹϾ��Դϴ�.
        return rotation_axis; // DirectX������ w=0�� ���͸� ���� ���ʹϾ����� ���
    }

    // 5. �Ϲ����� ���
    // ȸ����
    XMVECTOR rotation_axis = XMVector3Cross(fromDirection, toDirection);

    // w (�Ǽ���) ���
    // sqrt(len_sq(from) * len_sq(to))�� 1.0�̹Ƿ� (����ȭ��), 1.0 + dot_product
    float w = 1.0f + dot_product;

    XMVECTOR q = XMVectorSetW(rotation_axis, w);

    // 6. ���� ���ʹϾ� ����ȭ
    return XMQuaternionNormalize(q);
}

inline XMVECTOR LookRotationUnityLike(FXMVECTOR forward, FXMVECTOR up)
{
    // ����ȭ
    XMVECTOR f = XMVector3Normalize(forward);
    XMVECTOR r = XMVector3Normalize(XMVector3Cross(up, f));  // Unity ���: right = up x forward
    if (XMVectorGetX(XMVector3Length(r)) < 0.01f) {
        return QuaternionFromToRotation(XMVectorSet(0, 0, 1, 0), f);
    }

    XMVECTOR u = XMVector3Cross(f, r);                        // ������ up

    // ȸ�� ��� ����
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
    // 1. ȸ�� ������ �������� ��ȯ
    float angleRadians = XMConvertToRadians(angleDegrees);
    
    if (XMVector3Equal(axis, XMVectorZero())) {
        return;
    }

    // 2. ��� ������ ȸ�� ���ʹϾ� ����
    Quaternion q = Quaternion::CreateFromAxisAngle(axis, angleRadians);

    // 3. ��ġ ȸ��
    Vector3 dir = position - pivot;         // �ǹ������� ��� ��ġ
    dir = Vector3::Transform(dir, q);       // ȸ��
    position = pivot + dir;                 // �� ��ġ

    // 4. ȸ���� ����
    rotation = q * rotation;                // ���ʹϾ� ��: q �� rotation �������� ����
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


