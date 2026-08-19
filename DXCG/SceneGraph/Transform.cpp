#include "SceneGraph/Transform.h"

XMMATRIX Transform::GetLocalMatrix() const
{
    XMVECTOR s = XMLoadFloat3(&mScale);
    XMVECTOR r = XMLoadFloat4(&mRotation);
    XMVECTOR t = XMLoadFloat3(&mPosition);
    return XMMatrixScalingFromVector(s) * XMMatrixRotationQuaternion(r) * XMMatrixTranslationFromVector(t);
}

XMMATRIX Transform::GetWorldMatrix() const
{
    XMMATRIX local = GetLocalMatrix();
    if (mParent)
        return local * mParent->GetWorldMatrix(); 
    return local;
}

void Transform::SetParent(Transform* parent)
{
    if (mParent)
    {
        auto& siblings = mParent->mChildren;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
    }
    mParent = parent;
    if (parent)
        parent->mChildren.push_back(this);
}
