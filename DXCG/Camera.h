#pragma once
#include <DirectXMath.h>
#include "InputManager.h"

using namespace DirectX;

class Camera
{
public:
	Camera() = default;
	~Camera() = default;
	Camera(const Camera& rhs) = delete;
	Camera& operator=(const Camera& rhs) = delete;

	void SetPosition(float x, float y, float z) { mPosition = { x, y, z }; }
	void SetPosition(const XMVECTOR& pos) { XMStoreFloat3(&mPosition, pos); }
	void SetPosition(const XMFLOAT3& pos) { mPosition = pos; }

	void SetLens(float foV, float aspect, float nearZ, float farZ);

	XMVECTOR GetPosition() const { return XMLoadFloat3(&mPosition); }
	XMFLOAT3 GetPosition3f() const { return mPosition; }

	XMMATRIX GetView() const { return XMLoadFloat4x4(&mView); }
	XMFLOAT4X4 GetView4x4() const { return mView; }
	
	XMMATRIX GetProj() const { return XMLoadFloat4x4(&mProj); }
	XMFLOAT4X4 GetProj4x4() const { return mProj; }
	
	void Update(float dt);

private:
	void Strafe(float d);
	void Walk(float d);
	void Pitch(float angle);
	void RotateY(float angle);

	void UpdateViewMatrix();
	void UpdateProjMatrix();

private:
	XMFLOAT3 mPosition = { 0.0f, 0.0f, -20.0f };
	XMFLOAT3 mRight = { 1.0f, 0.0f, 0.0f };
	XMFLOAT3 mUp = { 0.0f, 1.0f, 0.0f };
	XMFLOAT3 mLook = { 0.0f, 0.0f, 1.0f };
	
	float mFoV;
	float mNearZ;
	float mFarZ;
	float mAspect;
	float mSpeed = 10.0f;

	XMFLOAT4X4 mView;
	XMFLOAT4X4 mProj;

	bool mViewDirty = true;
};

