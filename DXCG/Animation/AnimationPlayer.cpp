#include "Animation/AnimationPlayer.h"

using namespace DirectX;

namespace
{
	//몇 번 키와 몇 번 키 사이인지 구하는 함수
	template<typename T>
	size_t FindKeyIndex(const std::vector<Keyframe<T>>& keys, float t)
	{
		for (size_t i = 0; i + 1 < keys.size(); ++i)
			if (t < keys[i + 1].Time)
				return i;
		
		return keys.empty() ? 0 : keys.size() - 1;
	}

	// 그 구간에서 몇% 지점인지 구하는 함수
	template<typename T>
	float GetLerpFactor(const std::vector<Keyframe<T>>& keys, size_t i, float t)
	{
		if (i + 1 >= keys.size()) return 0.0f;
		
		float span = keys[i + 1].Time - keys[i].Time;
		if (span <= 0.0f) return 0.0f;

		float f = (t - keys[i].Time) / span;
		return (f < 0.0f) ? 0.0f : (f > 1.0f ? 1.0f : f);
	}

	// 위 둘을 써서 실제 값 계산
	XMVECTOR InterpolateScale(const BoneAnimation& anim, float t)
	{
		if (anim.ScaleKeys.empty()) return XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);
		if (anim.ScaleKeys.size() == 1)
			return XMLoadFloat3(&anim.ScaleKeys[0].Value);

		size_t i = FindKeyIndex(anim.ScaleKeys, t);
		float f = GetLerpFactor(anim.ScaleKeys, i, t);

		XMVECTOR a = XMLoadFloat3(&anim.ScaleKeys[i].Value);
		XMVECTOR b = XMLoadFloat3(&anim.ScaleKeys[std::min(i + 1, anim.ScaleKeys.size() - 1)].Value);
		return XMVectorLerp(a, b, f);
	}

	// 스케일도 위와 동일 (XMVectorLerp)
	XMVECTOR InterpolatePosition(const BoneAnimation& anim, float t) 
	{
		if (anim.PositionKeys.empty()) return XMVectorZero();
		if (anim.PositionKeys.size() == 1)
			return XMLoadFloat3(&anim.PositionKeys[0].Value);

		size_t i = FindKeyIndex(anim.PositionKeys, t);
		float f = GetLerpFactor(anim.PositionKeys, i, t);

		XMVECTOR a = XMLoadFloat3(&anim.PositionKeys[i].Value);
		XMVECTOR b = XMLoadFloat3(&anim.PositionKeys[std::min(i + 1, anim.PositionKeys.size() - 1)].Value);
		return XMVectorLerp(a, b, f);
	}

	// 회전만 다르다: 쿼터니언은 선형 보간하면 안 되고 SLERP를 써야 한다.
	// 선형으로 섞으면 정규화가 깨져서 회전이 찌그러지고, 각속도도 일정하지 않다.
	XMVECTOR InterpolateRotation(const BoneAnimation& anim, float t)
	{
		if (anim.RotationKeys.empty()) return XMQuaternionIdentity();
		if (anim.RotationKeys.size() == 1)
			return XMLoadFloat4(&anim.RotationKeys[0].Value);

		size_t i = FindKeyIndex(anim.RotationKeys, t);
		float  f = GetLerpFactor(anim.RotationKeys, i, t);

		XMVECTOR a = XMLoadFloat4(&anim.RotationKeys[i].Value);
		XMVECTOR b = XMLoadFloat4(&anim.RotationKeys[std::min(i + 1, anim.RotationKeys.size() - 1)].Value);
		return XMQuaternionNormalize(XMQuaternionSlerp(a, b, f));
	}
}

void AnimationPlayer::SetClip(const Skeleton* skeleton, const AnimationClip* clip)
{
	mSkeleton = skeleton;
	mClip = clip;
	mTimeSeconds = 0.0f;

	if (mSkeleton != nullptr)
		mBoneTransforms.assign(mSkeleton->Bones.size(), MathHelper::Identity4x4());
	else
		mBoneTransforms.clear();

	ComputeBoneTransforms();
}

void AnimationPlayer::Update(float dt)
{
	if (!IsPlaying()) return;

	mTimeSeconds += dt;

	const float duration = mClip->GetDurationSeconds();

	if (duration > 0.0f)
	{
		if (mLoop)
			mTimeSeconds = fmodf(mTimeSeconds, duration);
		else if (mTimeSeconds > duration)
			mTimeSeconds = duration;
	}

	ComputeBoneTransforms();
}

void AnimationPlayer::ComputeBoneTransforms()
{
	if (mSkeleton == nullptr) return;

	const size_t boneCount = mSkeleton->Bones.size();
	mBoneTransforms.resize(boneCount);

	// 각 본의 "모델 공간 월드 행렬"을 임시로 담는다.
	std::vector<XMMATRIX> worldMats(boneCount);

	// 클립 시간은 초가 아니라 '틱' 단위다.
	const float ticks = mClip ? (mTimeSeconds * mClip->TicksPerSecond) : 0.0f;

	for (size_t i = 0; i < boneCount; ++i)
	{
		const BoneInfo& bone = mSkeleton->Bones[i];

		XMMATRIX local;

		// 이 본에 애니메이션 채널이 있으면 키프레임에서, 없으면 기본 자세를 쓴다.
		const BoneAnimation* anim =
			(mClip && i < mClip->BoneAnimations.size()) ? &mClip->BoneAnimations[i] : nullptr;

		if (anim != nullptr && !anim->IsEmpty())
		{
			XMVECTOR s = InterpolateScale(*anim, ticks);
			XMVECTOR r = InterpolateRotation(*anim, ticks);
			XMVECTOR p = InterpolatePosition(*anim, ticks);

			// 스케일 -> 회전 -> 이동 순서. 반대로 하면 회전이 위치를 끌고 간다.
			local = XMMatrixScalingFromVector(s)
				* XMMatrixRotationQuaternion(r)
				* XMMatrixTranslationFromVector(p);
		}
		else
		{
			local = XMLoadFloat4x4(&bone.LocalBindTransform);
		}

		// 부모 월드에 누적.
		// BuildBoneHierarchy가 부모를 자식보다 앞에 넣어줬으므로
		// 앞에서부터 한 번만 훑어도 부모가 이미 계산돼 있다.
		if (bone.ParentIndex >= 0)
			worldMats[i] = local * worldMats[bone.ParentIndex];
		else
			worldMats[i] = local;
	}

	// 최종 팔레트.
	// OffsetMatrix로 정점을 본 로컬로 되돌린 뒤, 현재 본 자세를 적용하고,
	// 마지막에 루트 보정을 되돌린다.
	XMMATRIX globalInv = XMLoadFloat4x4(&mSkeleton->GlobalInverseTransform);

	for (size_t i = 0; i < boneCount; ++i)
	{
		XMMATRIX offset = XMLoadFloat4x4(&mSkeleton->Bones[i].OffsetMatrix);
		XMStoreFloat4x4(&mBoneTransforms[i], offset * worldMats[i] * globalInv);
	}
}