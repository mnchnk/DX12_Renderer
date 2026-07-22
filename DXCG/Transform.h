#pragma once
#include <DirectXMath.h>
#include <vector>
#include <algorithm>

using namespace DirectX;

class Transform
{
public:
	Transform() = default;
	~Transform() = default;
	Transform(const Transform& rhs) = delete;
	Transform& operator=(const Transform& rhs) = delete;

public:
	void SetPosition(const XMFLOAT3& pos) { mPosition = pos; }
	void SetRotation(const XMFLOAT4& rot) { mRotation = rot; }
	void SetScale(const XMFLOAT3& scale) { mScale = scale; }

	void SetPosition(const XMVECTOR& pos) { XMStoreFloat3(&mPosition, pos); }
	void SetRotation(const XMVECTOR& rot) { XMStoreFloat4(&mRotation, rot); }
	void SetScale(const XMVECTOR& scale) { XMStoreFloat3(&mScale, scale); }

	const XMFLOAT3& GetPosition() const { return mPosition; }
	const XMFLOAT4& GetRotation() const { return mRotation; }
	const XMFLOAT3& GetScale() const { return mScale; }

	XMMATRIX GetLocalMatrix() const;
	XMMATRIX GetWorldMatrix() const;

	void SetParent(Transform* parent);
	Transform* GetParent() const { return mParent; }
	const std::vector<Transform*>& GetChildren() const { return mChildren; }

private:
	XMFLOAT3 mPosition = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4 mRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
	XMFLOAT3 mScale = { 1.0f, 1.0f, 1.0f };

	Transform* mParent = nullptr;
	std::vector<Transform*> mChildren;
};

